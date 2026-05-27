// ShotDetector.cpp: implementation of the CShotDetector class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "SVEDet.h"
#include "ShotDetector.h"
#include "ChildFrm.h"
#include "Global.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif


CShotDetector::CShotDetector()
{
	// COM initialization
	CoInitialize(NULL);

}

CShotDetector::~CShotDetector()
{
	CoUninitialize();
}

/*
*	镜头边缘检测
*	帧间差评价,只需要检测突变
*
*	HWND		hWnd				-- 接受消息窗口
*	CString&	strVideoPathName	-- 视频文件路径
*	DWORD		dwSegStartFrm		-- 检测的段落的起始位置(帧号)
*	DWORD		dwSegEndFrm			-- 检测的段落终止位置(帧号), 如果为 0, 则表示检测整个视频文件
*	DWORD		dwVideoFrmTotal		-- 视频长度(帧)
*	double		dfVideoTimeTotal	-- 视频长度(秒)
*	double		dfFrameRate			-- 帧率(frame per second)
*	double		dfTimePerFrm		-- 帧长(second per frame)
*	long		lVidStrmIdx			-- 视频流索引值
*	int			nVideoHeight		-- 视频帧高度
*	int			nVideoWidth			-- 视频帧宽度
*	long		lImgBufSize			-- 视频帧DIB字节数(信息头和数据)
*
*	确定一个镜头边缘的规则
*	1. 在局部帧间差窗口中是最大, 并且
*	2. 此最大值 >= mean + alpha * std, mean, std 分别是窗口中数据的均值和标准差, alpha是门限参数
*	3. 
*
*	返回一个BOOL量,正常返回TRUE,出错则返回FALSE
*	检测到的镜头边缘(帧号),用消息传递给主框架

*	1. 当前帧数据消息	WM_IMG_DISPLAY
*		wParam = 0, lParam = 0	==>	开始处理
*		wParam !=0, lParam >=0	==> 发送数据, wParam 为当前帧数据DIB, lParam 为帧号
*		wParam = 0, lParam = 1  ==> 处理结束
*
*	2. 镜头边缘消息定义 WM_SHOT_DETECT
*		wParam = 0, lParam = 0	==>	开始检测
*		wParam !=0, lParam >=0	==> 发送数据, wParam 为镜头边缘帧数据DIB, lParam 为帧号
*		wParam = 0, lParam = 1  ==> 检测结束
*/
bool CShotDetector::ShotDet(HWND hWnd, CString& strVideoPathName, DWORD dwSegStartFrm, DWORD dwSegEndFrm, 
							DWORD dwVideoFrmTotal, double dfVideoTimeTotal, 
							double dfFrameRate, double dfTimePerFrm, long lVidStrmIdx,
							int nVideoHeight, int nVideoWidth, long	lImgBufSize)
{
	// ******************************************************************************* //
	// IMediaDet Initialization

	USES_CONVERSION;

	if(strVideoPathName.IsEmpty()) {
		AfxMessageBox("No intput video file!", MB_OK);
		return false;
	}

    HRESULT hr;

    // create a media detector
    CComPtr< IMediaDet > pDet;
	hr = CoCreateInstance( CLSID_MediaDet, NULL, CLSCTX_INPROC_SERVER, 
                           IID_IMediaDet, (void**) &pDet );
	if(FAILED(hr)) {
		AfxMessageBox(" ERROR! Failed to Create COM Instance.", MB_OK);
		return false;
	}
 
    // set filename and look for a video stream
    long Streams = 0;
    BOOL bVideoFound = FALSE;
    hr = pDet->put_Filename( T2W( strVideoPathName ) );
	if(FAILED(hr)) {
		AfxMessageBox("ERROR! IMediaDet failed to put file name.", MB_OK);
		return false;
	}

    hr =pDet->get_OutputStreams( &Streams );
	if(FAILED(hr)) {
		AfxMessageBox("ERROR! IMediaDet failed to get output Stream.", MB_OK);
		return false;
	}

	// 直接对视频流存取
	hr = pDet->put_CurrentStream( lVidStrmIdx );
	if(FAILED(hr)) {
		AfxMessageBox("ERROR! IMediaDet failed to put current Stream.", MB_OK);
		return false;
	}

    // this method will change the MediaDet to go into "sample grabbing mode" at time 0.
    hr = pDet->EnterBitmapGrabMode( 0.0 );
	if(FAILED(hr)) {
		AfxMessageBox("ERROR! IMediaDet failed to enter bitmap GrabMode.", MB_OK);
		return false;
	}
	
    // ask for the sample grabber filter that we know lives inside the graph made by the MediaDet
    CComPtr< ISampleGrabber > pGrabber;
    hr = pDet->GetSampleGrabber( &pGrabber );
	if(FAILED(hr)) {
		AfxMessageBox("ERROR! IMediaDet failed to get Sample Grabber.", MB_OK);
		return false;
	}

    // set the callback (our COM object callback)    
    CComQIPtr< IBaseFilter, &IID_IBaseFilter > pFilter( pGrabber );

	// find the filter graph interface from the sample grabber filter
    FILTER_INFO fi;
    memset( &fi, 0, sizeof( fi ) );
    hr = pFilter->QueryFilterInfo( &fi );

    // Release the filter's graph reference
    if( fi.pGraph ) fi.pGraph->Release( );
    IFilterGraph * pGraph = fi.pGraph;

   	// End of IMediaDet Initialization
	// ****************************************************************************** //

	// ****************************************************************************** //
	// Parameters setup
	double	th_min_shot_time = 1.0;				// 最短的镜头长度(时间单位秒)
	int th_min_shot_frm  = (int)(th_min_shot_time * dfFrameRate);
	// 直方图统计参数
	double	Tb_hist_global = 25.0;	// 镜头边缘的分割门限,全局
	double  Tb_hist_local;		// 镜头边缘的分割门限,局部
	double	Hist_diff_mean=0.0, Hist_diff_std=0.0;	// 局部窗口帧间差的均值和方差
	double	alpha_hist  = 2.5;	// 确定门限的参数, Tb = mean + alpha*std
	// 灰度统计参数(亮度补偿)
	double	Tb_frm_global = 15.0;	// 镜头边缘的分割门限,全局
//	int Tb_frm_local;		// 镜头边缘的分割门限,局部
	double	Frm_diff_mean=0.0, Frm_diff_std=0.0;	// 局部窗口帧间差的均值和方差
	double	alpha_frm  = 2.5;	// 确定门限的参数, Tb = mean + alpha*std

	int nWinSize = 15;	// 9, 21, ...
	CMathTool<DWORD, 15> Stat_HistDiff;	// 局部窗口中图像直方图差别统计器
	CMathTool<DWORD, 15> Stat_FrmDiff;	// 局部窗口中图像差别统计器(灰度补偿)	

	int		nBin = 64;
	int		nChannel = 3;
	DWORD	*CurImgHist = new DWORD[nChannel*nBin];
	memset(CurImgHist, 0, nChannel*nBin*sizeof(DWORD));
	DWORD	*RefImgHist	= new DWORD[nChannel*nBin];
	memset(RefImgHist, 0, nChannel*nBin*sizeof(DWORD));
	
	// ****************************************************************************** //
	// Data Allocation of Shot Detection
	BOOL bInProc = FALSE;	// 进行处理标志, 处理之前为false, 开始处理时为true

	// lBufferSize == 包含bmp信息头和数据的尺寸
	char *pCurImgBuf = new char[lImgBufSize];
	if (!pCurImgBuf)  {
		AfxMessageBox("ERROR! Failed to allocate memory.", MB_OK);
		return false;
	}
	char *pRefImgBuf = new char[lImgBufSize];
	if (!pRefImgBuf)  {
		AfxMessageBox("ERROR! Failed to allocate memory.", MB_OK);
		return false;
	}

	int BmpWidthBytes = (nVideoWidth*24+31)/32*4;;

	// 发送消息,开始显示当前处理图像
	SendMessage(hWnd, WM_IMG_DISPLAY, 0, 0);
	// 开始检测
	SendMessage(hWnd, WM_SHOT_DETECT, 0, 0);

	double dStreamTime = 0.0;
	DWORD	dwStep = 1;	 // frame step for shot detection
	DWORD	dwCurFrmIdx = 0;	// 当前帧在计算序列中的索引号
	DWORD	dwHitFrmIdx = 0, dwHitHistDiff, dwHitFrmDiff;// 被击中的帧号(因为当前处理帧优先与击中的考察帧 WinSize/2 个索引值)
							// 实际给出的镜头边缘是对击中值在局部窗口中的考察结果
	DWORD	dwHistDiff;
	DWORD	dwFrmDiff;
	DWORD	dwShotBoundary;	// 镜头边缘的帧号
	for(m_dwCurFrmNo = dwSegStartFrm, dwCurFrmIdx = 0; m_dwCurFrmNo < dwSegEndFrm; m_dwCurFrmNo+=dwStep, dwCurFrmIdx++) {	

		dStreamTime = dfVideoTimeTotal*m_dwCurFrmNo / dwVideoFrmTotal;
		
		try {
			hr = pDet->GetBitmapBits(dStreamTime, 0, pCurImgBuf, nVideoWidth, nVideoHeight);
		}
		catch (...) {
			delete [] pCurImgBuf;
			AfxMessageBox("ERROR! IMediaSeeking failed to get BitmapBits.", MB_OK);
			return false;
		}
		if (SUCCEEDED(hr))	{
			/*== send message to main frame	
			// 准备处理时(bInProc = FALSE), mParam = NULL, lParam = ImageSize
			// 处理中时  (bInProc = FALSE), mParam = pnOutBuf, lParam = dwCurFrmNo
			// 处理完后  (bProcEnd = TRUE), mParam = NULL, lParam = -1
			*/
			if(!bInProc) {
			
				// 发送第一帧数据, 视频第一帧作为第一个镜头开始
				SendMessage(hWnd, WM_SHOT_DETECT, (WPARAM)pCurImgBuf, m_dwCurFrmNo);	
				
				bInProc = TRUE;
			}

			// 发送当前帧数据以供监测显示
			SendMessage(hWnd, WM_IMG_DISPLAY, (WPARAM)pCurImgBuf, m_dwCurFrmNo);

			// ==== 直方图统计特性 =================================================================
			dwHistDiff = 0;
			ImgHist(pCurImgBuf, nVideoHeight, nVideoWidth, CurImgHist, nBin, nChannel);
			if( dwCurFrmIdx == 0 ) {				
				memcpy(RefImgHist, CurImgHist, nChannel*nBin*sizeof(DWORD));
			}
			dwHistDiff = HistDiff(CurImgHist, RefImgHist, nVideoHeight, nVideoWidth, nBin, nChannel);

			// 保存数据到局部数组
			Stat_HistDiff.m_DataArray[dwCurFrmIdx%nWinSize] = dwHistDiff;
			// 计算窗口中的均值和方差
			if( dwCurFrmIdx < (DWORD)nWinSize ) {
				Hist_diff_mean = Hist_diff_std = 0.0;					
			}
			else {
				// 统计均值方差时除掉击中值本身
				Stat_HistDiff.MeanStd(Hist_diff_mean, Hist_diff_std, -1 /* (dwCurFrmIdx-m_nWinSize/2)%m_nWinSize */);					
			}

			// ==== 亮度统计特性 =================================================================
			dwFrmDiff = 0;			
			if( dwCurFrmIdx == 0 ) {				
				memcpy(pRefImgBuf, pCurImgBuf, lImgBufSize);
			}
			dwFrmDiff = FrmDiff(pCurImgBuf, pRefImgBuf, nVideoHeight, nVideoWidth);

			// 保存数据到局部数组
			Stat_FrmDiff.m_DataArray[dwCurFrmIdx%nWinSize] = dwFrmDiff;
			
			/*
			// 计算窗口中的均值和方差
			if( dwCurFrmIdx < (DWORD)nWinSize ) {
				Frm_diff_mean = Frm_diff_std = 0.0;					
			}
			else {
				// 统计均值方差时除掉击中值本身
				Stat_FrmDiff.MeanStd(Frm_diff_mean, Frm_diff_std, -1 );					
			}
			*/
		    
		
			// 只有当局部窗口中数据填满时才开始考察中间值
			if( dwCurFrmIdx >= (DWORD)nWinSize )
			{
				if( dwCurFrmIdx == 527 ) {
					int skkkhk = 0;
				}

				dwHitFrmIdx = (dwCurFrmIdx - nWinSize/2) % nWinSize;	// 击中值的索引号
				dwHitHistDiff = Stat_HistDiff.m_DataArray[dwHitFrmIdx];
				dwHitFrmDiff  = Stat_FrmDiff.m_DataArray[dwHitFrmIdx];

				Tb_hist_local = Hist_diff_mean + alpha_hist * Hist_diff_std;
				//Tb_frm_local  = (int)(Frm_diff_mean + alpha_frm * Frm_diff_std);

				// 直方图之差很大,并且亮度补偿之后的灰度差也很大
				if( ( dwHitHistDiff > Tb_hist_local && dwHitHistDiff > Tb_hist_global ) &&\
					( dwHitFrmDiff >= Tb_frm_global ) )
				{
					//bShotBoundary = TRUE;
					dwShotBoundary = m_dwCurFrmNo - nWinSize/2 * dwStep;

					// 取得镜头边缘的数据
					double	tmp_time = dStreamTime - nWinSize/2 * dwStep * dfTimePerFrm;
					pDet->GetBitmapBits(tmp_time, 0, pCurImgBuf, nVideoWidth, nVideoHeight);

					// 发送消息, 镜头边缘
					SendMessage(hWnd, WM_SHOT_DETECT, (WPARAM)pCurImgBuf, dwShotBoundary);
				}
			}							
				
			//==数据更新============				
			memcpy(RefImgHist, CurImgHist, nChannel*nBin*sizeof(DWORD));
			memcpy(pRefImgBuf, pCurImgBuf, lImgBufSize);
		}  		
	}

	// 检测结束,发送检测结束消息
	SendMessage(hWnd, WM_SHOT_DETECT, 0, 1);
	SendMessage(hWnd, WM_IMG_DISPLAY, 0, 1);
		
	delete []pCurImgBuf;	pCurImgBuf = NULL;

	delete []CurImgHist;	CurImgHist = NULL;
	delete []RefImgHist;	RefImgHist = NULL;

	pDet.Release();

	return true;
}

// 全局RGB颜色直方图
void CShotDetector::ImgHist(char *pDIB, int nHeight, int nWidth, DWORD *Hist, int nBin, int nChannel)
{
/*
	memset(pHist,0,256*sizeof(int));
	int imageSize=Width*Height;
	for(int i=0;i<imageSize;i++)
		pHist[srcImage[i]]++;
*/
	BYTE red, green, blue;
	int r,c;
	int BmpWidthBytes = ((nWidth*24) + 31) / 32 * 4;

	memset(Hist, 0, nChannel*nBin*sizeof(DWORD));
	
	if( nChannel == 1 )
	{
		
		for(r = 0; r < nHeight; r++) {
			for(c = 0; c < nWidth; c++) {

				red = (BYTE)(*(pDIB + sizeof(BITMAPINFOHEADER) + r*BmpWidthBytes + 3*c));
				green = (BYTE)(*(pDIB + sizeof(BITMAPINFOHEADER) + r*BmpWidthBytes + 3*c+1));
				blue = (BYTE)(*(pDIB + sizeof(BITMAPINFOHEADER) + r*BmpWidthBytes + 3*c+2));

				int gray = (red + green + blue)/3;
				gray = gray/(256/nBin);
			
				Hist[gray]++;
			}
		}
	}
	else if( nChannel == 3 )
	{
		for(r = 0; r < nHeight; r++) {
			for(c = 0; c < nWidth; c++) {
				red = (BYTE)(*(pDIB + sizeof(BITMAPINFOHEADER) + r*BmpWidthBytes + 3*c));
				red = red/(256/nBin);
				Hist[red]++;

				green = (BYTE)(*(pDIB + sizeof(BITMAPINFOHEADER) + r*BmpWidthBytes + 3*c+1));
				green = green/(256/nBin);
				Hist[nBin + green]++;

				blue = (BYTE)(*(pDIB + sizeof(BITMAPINFOHEADER) + r*BmpWidthBytes + 3*c+2));
				blue = blue/(256/nBin);
				Hist[2*nBin + red]++;
			}
		}
	}
	
}

// 直方图相似性度量(直方图差)
DWORD CShotDetector::HistDiff(DWORD *CurHist, DWORD *RefHist, int nHeight, int nWidth, int nBin, int nChannel)
{
	DWORD dwHistDiff = 0;

	int i;
	for(i = 0; i < nChannel*nBin; i++ ) {	
		dwHistDiff += abs(CurHist[i] - RefHist[i]);
	}

	// 将范围设为[0, 100]
	dwHistDiff = (100*dwHistDiff) / (2*nChannel*nHeight*nWidth);

	return dwHistDiff;
}

// 灰度距离度量(加上亮度补偿)
DWORD CShotDetector::FrmDiff(char *pCurImgBuf, char *pRefImgBuf, int nHeight, int nWidth)
{
	int r, c;
	int WidthBytes = (((nWidth*24) + 31) / 32 * 4);

	DWORD dwRefGrayAvg, dwCurGrayAvg;
	tagRGBTRIPLE	*pCurRGB, *pRefRGB;
	BYTE red, green, blue;
	int cur_gray, ref_gray;

	dwCurGrayAvg = 0;
	pCurRGB = (tagRGBTRIPLE *)(pCurImgBuf + sizeof(BITMAPINFOHEADER));
	for(r = 0; r < nHeight; r++) {
		for(c = 0; c < nWidth; c++) {
			red = pCurRGB->rgbtRed;
			green = pCurRGB->rgbtGreen;
			blue = pCurRGB->rgbtBlue;

			// BGR 顺序存储
			//blue = (BYTE)(*(pCurImgBuf + sizeof(BITMAPINFOHEADER) + r*WidthBytes + 3*c));
			//green = (BYTE)(*(pCurImgBuf + sizeof(BITMAPINFOHEADER) + r*WidthBytes + 3*c+1));
			//red = (BYTE)(*(pCurImgBuf + sizeof(BITMAPINFOHEADER) + r*WidthBytes + 3*c+2));

			cur_gray = (6*red + 3*green + blue) / 10;

			dwCurGrayAvg += cur_gray;

			pCurRGB++;
		}
	}
	dwCurGrayAvg = dwCurGrayAvg/(nHeight*nWidth);

	dwRefGrayAvg = 0;
	pRefRGB = (tagRGBTRIPLE *)(pRefImgBuf + sizeof(BITMAPINFOHEADER));
	for(r = 0; r < nHeight; r++) {
		for(c = 0; c < nWidth; c++) {
			red = pRefRGB->rgbtRed;
			green = pRefRGB->rgbtGreen;
			blue = pRefRGB->rgbtBlue;

			// BGR 顺序存储
			//blue = (BYTE)(*(pCurImgBuf + sizeof(BITMAPINFOHEADER) + r*WidthBytes + 3*c));
			//green = (BYTE)(*(pCurImgBuf + sizeof(BITMAPINFOHEADER) + r*WidthBytes + 3*c+1));
			//red = (BYTE)(*(pCurImgBuf + sizeof(BITMAPINFOHEADER) + r*WidthBytes + 3*c+2));

			ref_gray = (6*red + 3*green + blue) / 10;

			dwRefGrayAvg += ref_gray;

			pRefRGB++;
		}
	}
	dwRefGrayAvg = dwRefGrayAvg/(nHeight*nWidth);

	int avg_diff = (int)(dwCurGrayAvg - dwRefGrayAvg);

	DWORD	dwFrmDiff = 0;
	pRefRGB = (tagRGBTRIPLE *)(pRefImgBuf + sizeof(BITMAPINFOHEADER));
	pCurRGB = (tagRGBTRIPLE *)(pCurImgBuf + sizeof(BITMAPINFOHEADER));
	for(r = 0; r < nHeight; r++) {
		for(c = 0; c < nWidth; c++) {
			red = pCurRGB->rgbtRed;
			green = pCurRGB->rgbtGreen;
			blue = pCurRGB->rgbtBlue;		
			cur_gray = (6*red + 3*green + blue) / 10;

			red = pRefRGB->rgbtRed;
			green = pRefRGB->rgbtGreen;
			blue = pRefRGB->rgbtBlue;		
			ref_gray = (6*red + 3*green + blue) / 10;

			dwFrmDiff = dwFrmDiff + abs(cur_gray-avg_diff - ref_gray);

			pCurRGB++;
			pRefRGB++;
		}
	}
	dwFrmDiff = dwFrmDiff/(nHeight*nWidth);

	return dwFrmDiff;
}

