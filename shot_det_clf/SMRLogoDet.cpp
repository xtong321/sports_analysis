// SMRLogoDet.cpp: implementation of the CSMRLogoDet class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "SVEDet.h"
#include "SMRLogoDet.h"
#include "Global.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CSMRLogoDet::CSMRLogoDet()
{
	// COM initialization
	CoInitialize(NULL);
}

CSMRLogoDet::~CSMRLogoDet()
{
	CoUninitialize();
}

/*
*	自动自适应地提取慢速回放标志图像
*
*	思路:
*	step 1: 通过wipe检测,得到候选Logo中间图像
*	step 2: 从多个Logo候选图像中得到聚类中心,作为最终的结果
*
*	1. 当前帧数据消息	WM_IMG_DISPLAY
*		wParam = 0, lParam = 0	==>	开始处理
*		wParam !=0, lParam >=0	==> 发送数据, wParam 为当前帧数据DIB, lParam 为帧号
*		wParam = 0, lParam = 1 ==> 处理结束
*
*	2. Logo模板提取消息定义 WM_LOGO_TEMPLATE
*		wParam = 0, lParam = 0	==>	开始检测
*		wParam !=0, lParam >=0	==> 发送候选数据, wParam 为候数据DIB, lParam 为帧号
*		wParam !=0, lParam = -1	==> 发送最后结果, wParam 为候数据DIB, lParam 为最后结果标志
*		wParam = 0, lParam = -1	==> 发送最后结果, 检测失败
*		wParam = 0, lParam = 1 ==> 检测结束
*
*	参数说明:
*	HWND		hWnd				-- 调用窗口,接收消息窗口
*   CString&	strVideoPathName	-- 视频文件名
*	DWORD		dwVideoFrmTotal		-- 视频长度(帧)
*   double		dfVideoTimeTotal	-- 视频长度(时间,秒)
*	double		dfFrameRate			-- 帧率(frame per second, fps)
*   double		dfTimePerFrm		-- 帧时(second per fame, spf)
*   long		lVidStrmIdx			-- 视频流在多媒体流中的索引
*	int			nVideoHeight		-- 视频帧高度
*   int			nVideoWidth			-- 视频帧宽度
*   long		lImgBufSize			-- 一帧视频DIB的字节大小(信息头+数据)
*	
*/
#define LOGODET_DEBUG 1		// 标志调试版本
bool CSMRLogoDet::SMRLogoTemplExtract(HWND hWnd, CString& strVideoPathName,
							DWORD dwVideoFrmTotal, double dfVideoTimeTotal, 
							double dfFrameRate, double dfTimePerFrm, long lVidStrmIdx,
							int nVideoHeight, int nVideoWidth, long	lImgBufSize)
{	
	USES_CONVERSION;

	if(strVideoPathName.IsEmpty()) {
		AfxMessageBox("ERROR! Please Input a Video File Firstly.", MB_OK);
		return false;
	}

	WIN32_FIND_DATA FindFile;
	if( ::FindFirstFile((LPCTSTR)strVideoPathName, &FindFile) == INVALID_HANDLE_VALUE ) {
		AfxMessageBox("ERROR! Video File DO NOT Exist.", MB_OK);
		return false;
	}	
	
	double margin_ratio = 1.0/7;	//只取图像中间区域,边缘不计
	CRect	logoRect;	// logo 区域 (60, 15, 300, 215);	 
	logoRect.left	= (int)(nVideoWidth*margin_ratio);
	logoRect.right	= (int)(nVideoWidth*(1-margin_ratio));
	logoRect.top	= (int)(nVideoHeight * margin_ratio);
	logoRect.bottom	= (int)(nVideoHeight * (1-margin_ratio));
	
	bool bLogoDet = false;

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

   	// 第一次运行前清除数据 ========================================================
	// lBufferSize == 包含bmp信息头和数据的尺寸
	char *pCurImgBuf = new char[lImgBufSize];
	if (!pCurImgBuf)  {
		AfxMessageBox("ERROR! Failed to allocate memory.", MB_OK);
		return false;
	}

	char *pPreImgBuf = new char[lImgBufSize];

	int diff_red, diff_blue, diff_green;
	int r,c,i,j;
	int BmpWidthBytes = (nVideoWidth*24+31)/32*4;;
	// step 1 : Wipe Det ========================================================
#ifdef SOCCER_LOGO
	int th_wipe_frm_diff	= 1000;		// 描述wipe过程中帧间差的门限
	int th_logo_symm_diff	= 50;		// 候选logo图像的对称差门限(不得大于此)
	int th_logo_rect_mean	= 120;		// 候选logo图像的灰度均值门限(不得小于此)	
	int th_wipe_noise		= 2;		// wipe 过程中容许的噪声(个别帧间差小于th_wipe_frm_diff)
#else
	int th_wipe_frm_diff	= 1000;		// 描述wipe过程中帧间差的门限
	int th_logo_symm_diff	= 50;		// 候选logo图像的对称差门限(不得大于此)
	int th_logo_rect_mean	= 160;		// 候选logo图像的灰度均值门限(不得小于此)	
	int th_wipe_noise		= 3;		// wipe 过程中容许的噪声(个别帧间差小于th_wipe_frm_diff)
#endif

	int	th_wipe_dur_frm_min	= 7;		// wipe 延续的最小长度(14 is more accurate)
	int	th_wipe_dur_frm_max	= 30;		// wipe 延续的最大长度(一般不超过20,据观察)
	int th_wipe_logo_num	= 10;		// 利用wipe检测logo时需要候选wipe的最小个数

	double dfStreamTimeBias = 0.0;		// 偏离检测到的wipe中心的时间
	int logo_bias_frm = 0;	
	int wipe_bias_frm = 5;		// 在wipe中心图像左右偏移wipe_bias_frm的范围内选取中心图像// 偏离距离(以帧为单位)
	BOOL bCandLogo = FALSE;		// 候选logo的有效标志

	int logo_bias = 0, nCandLogo = 0;

	// 保存提取的wipe中心图像的数组
	BYTE **pArrWipeImgBuf = NULL;
	pArrWipeImgBuf = new BYTE*[th_wipe_logo_num];
	for(r=0; r<th_wipe_logo_num; r++) {
		pArrWipeImgBuf[r] = new BYTE[lImgBufSize];
	}		

	// 保存wipe周围图像的数组
	BYTE **pArrWipeBiasBuf = NULL;
	pArrWipeBiasBuf = new BYTE*[2*wipe_bias_frm+1];
	for(r=0; r<2*wipe_bias_frm+1; r++) {
		pArrWipeBiasBuf[r] = new BYTE[lImgBufSize];
	}

	DWORD	dwLogoFrmNum = 0;	// 提取出的logo图像的位置(帧号)
								// 最后一个表示总共运行到什么地方才提取出logo

	BOOL	bWipeFlag = FALSE;	// wipe检测标志(TRUE时,检测进行中;FALSE未进行检测)
	DWORD	dwFrmDiff, dwWipeStart, dwWipeEnd, dwWipePosFrm;
	double  dfWipePosTime;
	int		nWipe_noise_count = 0;

	// 开始处理,通知调用窗口
	SendMessage(hWnd, WM_IMG_DISPLAY, 0, 0);
	SendMessage(hWnd, WM_LOGO_TEMPLATE, 0, 0);

	double dStreamTime = 0.0;
	for(m_dwCurFrmNo = 0; m_dwCurFrmNo < dwVideoFrmTotal; m_dwCurFrmNo++) {	
		
		try {
			hr = pDet->GetBitmapBits(dStreamTime, 0, pCurImgBuf, nVideoWidth, nVideoHeight);
		}
		catch (...) {
			delete [] pCurImgBuf;
			AfxMessageBox("ERROR! IMediaSeeking failed to get BitmapBits.", MB_OK);
			return false;
		}
		if (SUCCEEDED(hr))	{
			
			// 发送当前处理图像数据
			SendMessage(hWnd, WM_IMG_DISPLAY, (WPARAM)pCurImgBuf, m_dwCurFrmNo);
			
			if(m_dwCurFrmNo == 0) {
				memcpy(pPreImgBuf, pCurImgBuf, lImgBufSize);			
			}
							
			dwFrmDiff = 0;
			for(r = nVideoHeight-1; r >= 0; r--) {
				for(c = 0; c < nVideoWidth; c++) {
					diff_blue  = (BYTE)(*(pCurImgBuf + sizeof(BITMAPINFOHEADER) + r*BmpWidthBytes + 3*c)) -   (BYTE)(*(pPreImgBuf  + sizeof(BITMAPINFOHEADER) + r*BmpWidthBytes + 3*c));
					diff_green = (BYTE)(*(pCurImgBuf + sizeof(BITMAPINFOHEADER) + r*BmpWidthBytes + 3*c+1)) - (BYTE)(*(pPreImgBuf  + sizeof(BITMAPINFOHEADER) + r*BmpWidthBytes + 3*c+1));
					diff_red   = (BYTE)(*(pCurImgBuf + sizeof(BITMAPINFOHEADER) + r*BmpWidthBytes + 3*c+2)) - (BYTE)(*(pPreImgBuf  + sizeof(BITMAPINFOHEADER) + r*BmpWidthBytes + 3*c+2));

					dwFrmDiff = dwFrmDiff + (diff_blue*diff_blue) + (diff_green*diff_green) + (diff_red*diff_red);			

				}
			}
			dwFrmDiff = (int)( dwFrmDiff / (3*nVideoHeight*nVideoWidth) );

			if(dwFrmDiff >= (DWORD)th_wipe_frm_diff && bWipeFlag == FALSE) {
				dwWipeStart = m_dwCurFrmNo;
				bWipeFlag   = TRUE;				// wipe检测开始
			}
			if( bWipeFlag ) {
				if( dwFrmDiff >= (DWORD)th_wipe_frm_diff ) {
					dwWipeEnd = m_dwCurFrmNo;
					nWipe_noise_count = 0;
				}
				else {
					nWipe_noise_count++;
				}
				// 如果超过噪声容限或者视频结束,则需要作出判断
				if( nWipe_noise_count >= th_wipe_noise || m_dwCurFrmNo == dwVideoFrmTotal-1 ) {
					// 判断wipe是否有效
					if( (int)(dwWipeEnd - dwWipeStart) >= th_wipe_dur_frm_min && (int)(dwWipeEnd - dwWipeStart) <= th_wipe_dur_frm_max) {	// valid
						dwWipePosFrm = (dwWipeStart + dwWipeEnd) / 2;
						dfWipePosTime = dwWipePosFrm * dfVideoTimeTotal / dwVideoFrmTotal;
						// 验证该wipe是否是有效的logo_tran
						for(logo_bias = -wipe_bias_frm; logo_bias <= wipe_bias_frm; logo_bias++) {
							// 取出近邻图像
							dfStreamTimeBias = dfWipePosTime + logo_bias*dfTimePerFrm;
							try {
								hr = pDet->GetBitmapBits(dfStreamTimeBias, 0, pCurImgBuf, nVideoWidth, nVideoHeight);
							}
							catch (...) {
								delete [] pCurImgBuf;
								AfxMessageBox("ERROR! IMediaSeeking failed to get BitmapBits.", MB_OK);
								return false;
							}
							if (SUCCEEDED(hr))	{
								memcpy(pArrWipeBiasBuf[logo_bias+wipe_bias_frm], (BYTE *)(pCurImgBuf), lImgBufSize);
							}
						}
						// 在上述wipe中找到中心对称像素差均值最小的图像
						DWORD tmpMinDiff = 256*nVideoHeight*nVideoWidth;
						int tmpWipeIndex = 0;
						for(int ii = 0; ii <2*wipe_bias_frm+1; ii++) {
							dwFrmDiff = 0;
							for(int rr = 0; rr < nVideoHeight; rr++) {
								for(int cc = 0; cc < nVideoWidth/2; cc++) {
									diff_blue  = (BYTE)(*(pArrWipeBiasBuf[ii]+sizeof(BITMAPINFOHEADER)+ rr*BmpWidthBytes + 3*cc)) - (BYTE)(*(pArrWipeBiasBuf[ii]+sizeof(BITMAPINFOHEADER) + rr*BmpWidthBytes + 3*(nVideoWidth-1-cc)));
									diff_green = (BYTE)(*(pArrWipeBiasBuf[ii]+sizeof(BITMAPINFOHEADER)+ rr*BmpWidthBytes + 3*cc+1)) - (BYTE)(*(pArrWipeBiasBuf[ii]+sizeof(BITMAPINFOHEADER) + rr*BmpWidthBytes + 3*(nVideoWidth-1-cc)+1));
									diff_red   = (BYTE)(*(pArrWipeBiasBuf[ii]+sizeof(BITMAPINFOHEADER)+ rr*BmpWidthBytes + 3*cc+2)) - (BYTE)(*(pArrWipeBiasBuf[ii]+sizeof(BITMAPINFOHEADER) + rr*BmpWidthBytes + 3*(nVideoWidth-1-cc)+2));

									dwFrmDiff = dwFrmDiff + abs(diff_blue) + abs(diff_green) + abs(diff_red);
								}
							}
							//dwFrmDiff = dwFrmDiff / (3*m_lHeight*m_lWidth/2);
							if(tmpMinDiff > dwFrmDiff) {
								tmpMinDiff = dwFrmDiff;
								tmpWipeIndex = ii;
							}
						}
						tmpMinDiff = tmpMinDiff / (3*nVideoHeight*nVideoWidth/2);
						// 进一步验证logo中心区域的平均灰度
						if(tmpMinDiff < (DWORD)th_logo_symm_diff) {
							dwFrmDiff = 0;
							for(int rr = logoRect.bottom-1; rr >= logoRect.top; rr--) {
								for(int cc = logoRect.left; cc < logoRect.right; cc++) {
									diff_blue  = (BYTE)(*(pArrWipeBiasBuf[tmpWipeIndex]+sizeof(BITMAPINFOHEADER)+ rr*BmpWidthBytes + 3*cc));
									diff_green = (BYTE)(*(pArrWipeBiasBuf[tmpWipeIndex]+sizeof(BITMAPINFOHEADER)+ rr*BmpWidthBytes + 3*cc+1));
									diff_red   = (BYTE)(*(pArrWipeBiasBuf[tmpWipeIndex]+sizeof(BITMAPINFOHEADER)+ rr*BmpWidthBytes + 3*cc+2));
									dwFrmDiff = dwFrmDiff + (diff_blue + 6*diff_green + 3*diff_red)/10;
								}
							}
							dwFrmDiff = dwFrmDiff / (logoRect.Height()*logoRect.Width());
						
							if(dwFrmDiff >= (DWORD)th_logo_rect_mean) {
								// 候选图像有效,记录之
								bCandLogo = TRUE;	

								tmpWipeIndex = wipe_bias_frm;	//  直接选定中间图像，而不做对称认证
								// 候选Logo的帧号
								//dwLogoFrmNum = dwWipePosFrm - wipe_bias_frm + tmpWipeIndex;
								dwLogoFrmNum = dwWipePosFrm;	// 直接选定中间图像

								memcpy(pArrWipeImgBuf[nCandLogo], pArrWipeBiasBuf[tmpWipeIndex], lImgBufSize);								
								
								// 发送消息,显示并记录该图像
								SendMessage(hWnd, WM_LOGO_TEMPLATE, (WPARAM)pArrWipeImgBuf[nCandLogo], dwLogoFrmNum);							

								// 候选Logo个数增加1
								nCandLogo++;

							}
						}											
					}
					else {	// invalid wipe, clear data
						dwWipeStart = 0;
						dwWipeEnd	= 0;
					}

					nWipe_noise_count = 0;
					bWipeFlag = FALSE;
					bCandLogo = FALSE;
				}
			}

			// 判断检测到的wipe个数是否满足条件
			if( nCandLogo >= th_wipe_logo_num )
				break;
					
			//==数据更新============				
			memcpy(pPreImgBuf, pCurImgBuf, lImgBufSize);
		}    

		dStreamTime += dfTimePerFrm;

		// 判断检测到的wipe个数是否满足条件
		if( nCandLogo >= th_wipe_logo_num )
			break;
	}
	
	for(r=0; r<2*wipe_bias_frm+1; r++) {
		delete []pArrWipeBiasBuf[r];
	}
	delete []pArrWipeBiasBuf;
	// end of step 1 ==============================================================================
	SendMessage(hWnd, WM_IMG_DISPLAY, 0, 1);	// 检测结束

	// step 2: 根据记录的wipe的位置,取出相应的图像,并聚类得到中心图像 =============================
	int th_logo_dist = 50;		// 两个候选logo之间容许的最大差别	

	if(nCandLogo <= 1) {
		bLogoDet = FALSE;
	}
	else {	// 有戏,可以继续		
		// 聚类
		CMatrix<DWORD> distMatrix(nCandLogo, nCandLogo);
		for(i=0; i<nCandLogo; i++) {
			for(j=i+1; j<nCandLogo; j++) {				
				dwFrmDiff = 0;
				for(r = logoRect.bottom-1; r >= logoRect.top; r--) {
					for(c = logoRect.left; c < logoRect.right; c++) {
						diff_blue  = (BYTE)(*(pArrWipeImgBuf[i] + sizeof(BITMAPINFOHEADER) + r*BmpWidthBytes + 3*c)) - (BYTE)(*(pArrWipeImgBuf[j] + sizeof(BITMAPINFOHEADER) + r*BmpWidthBytes + 3*c));
						diff_green = (BYTE)(*(pArrWipeImgBuf[i] + sizeof(BITMAPINFOHEADER) + r*BmpWidthBytes + 3*c+1)) - (BYTE)(*(pArrWipeImgBuf[j] + sizeof(BITMAPINFOHEADER) + r*BmpWidthBytes + 3*c+1));
						diff_red   = (BYTE)(*(pArrWipeImgBuf[i] + sizeof(BITMAPINFOHEADER) + r*BmpWidthBytes + 3*c+2)) - (BYTE)(*(pArrWipeImgBuf[j] + sizeof(BITMAPINFOHEADER) + r*BmpWidthBytes + 3*c+2));

						dwFrmDiff = dwFrmDiff + abs(diff_blue) + abs(diff_green) + abs(diff_red);
					}
				}
				dwFrmDiff = (int)( dwFrmDiff / (3*logoRect.Height()*logoRect.Width()) );

				distMatrix[i][j] = dwFrmDiff;
				distMatrix[j][i] = dwFrmDiff;
			}
		}

		DWORD minDist = 256;	// 各个wipe图像之间的最小距离(当最小距离很大时认为检测错误)
		for(i=0; i < nCandLogo; i++) {			
			for(j=i+1; j < nCandLogo; j++) {
				if( minDist > distMatrix[i][j] ) {
					minDist = distMatrix[i][j];
					//nCentIndex = i;
				}
			}
		}

		if(minDist > (DWORD)th_logo_dist)
			bLogoDet = false;
		else
			bLogoDet = true;

		// 寻找聚类中心
		DWORD minDistSum = 256*th_wipe_logo_num;
		int   nCentIndex = 0;
		for(i=0; i < nCandLogo; i++) {
			DWORD tmpSumDis = 0;
			for(j=0; j < nCandLogo; j++) {
				tmpSumDis += distMatrix[i][j];
			}
			if( minDistSum > tmpSumDis ) {
				minDistSum = tmpSumDis;
				nCentIndex = i;
			}
		}

		if(bLogoDet) {
			// 保存logo 图像, 平均几个距离很小图像,这样排除了个别背景的影响		

			int red_mean = 0, green_mean = 0, blue_mean = 0;
			int ImgCount_mean = 0;
			
			for(r=0; r < nVideoHeight; r++) {
				for(c=0; c < nVideoWidth; c++) {
					ImgCount_mean = 0;
					red_mean = 0; green_mean = 0; blue_mean = 0;
					for(i=0; i < nCandLogo; i++) {
						if( distMatrix[nCentIndex][i] < (DWORD)th_logo_dist ) {
							ImgCount_mean++;
							
							red_mean = red_mean + (BYTE)(*(pArrWipeImgBuf[i] + sizeof(BITMAPINFOHEADER) + r*BmpWidthBytes + 3*c));
							green_mean = green_mean + (BYTE)(*(pArrWipeImgBuf[i] + sizeof(BITMAPINFOHEADER) + r*BmpWidthBytes + 3*c+1));
							blue_mean = blue_mean + (BYTE)(*(pArrWipeImgBuf[i] + sizeof(BITMAPINFOHEADER) + r*BmpWidthBytes + 3*c+2));
						}
					}
					red_mean = (BYTE)(red_mean/ImgCount_mean);
					green_mean = (BYTE)(green_mean/ImgCount_mean);
					blue_mean = (BYTE)(blue_mean/ImgCount_mean);
					
					// RGB or BGR mode,由于前面是按顺序提取的值,没有考虑顺序,后面也不用考虑
					*(pArrWipeImgBuf[nCentIndex] + sizeof(BITMAPINFOHEADER) + r*BmpWidthBytes + 3*c) = (BYTE)red_mean;
					*(pArrWipeImgBuf[nCentIndex] + sizeof(BITMAPINFOHEADER) + r*BmpWidthBytes + 3*c+1) = (BYTE)green_mean;
					*(pArrWipeImgBuf[nCentIndex] + sizeof(BITMAPINFOHEADER) + r*BmpWidthBytes + 3*c+2) = (BYTE)blue_mean;								
				}
			}				
	
			//SaveBitmapBits((char *)(pArrWipeImgBuf[nCentIndex]), lImgBufSize, (LPTSTR)(LPCTSTR)strLogoImgPathName);
			//logoImg = ImgBufToiplImage((char *)(pArrWipeImgBuf[nCentIndex]));	
			SendMessage(hWnd, WM_LOGO_TEMPLATE, (WPARAM)(pArrWipeImgBuf[nCentIndex]), -1);
		}

		distMatrix.DeleteMatrix();
		for(r=0; r<th_wipe_logo_num; r++) {
			delete []pArrWipeImgBuf[r];
		}
		delete []pArrWipeImgBuf;
	}

	if( !bLogoDet ) {
		SendMessage(hWnd, WM_LOGO_TEMPLATE, 0, -1);		// 检测失败
	}
	SendMessage(hWnd, WM_LOGO_TEMPLATE, 0, 1);	// 检测结束
	
	if(pCurImgBuf != NULL) {
		delete []pCurImgBuf;		
	}
	pCurImgBuf = NULL;

	if(pPreImgBuf != NULL) {
		delete []pPreImgBuf;		
	}
	pPreImgBuf = NULL;

	pDet.Release();
	
	return bLogoDet;
}

/* 
*	利用回放标志模板提取整场比赛中的回放标志
*	
*	方法：
*	step 1 : 检测 LogoTrans
*	step 2 : 继续用LogoTempl验证
*			step 2.1 : 颜色差
*			step 2.2 : 比较不变矩
*
*	参数说明:
*	HWND		hWnd				-- 接受结果消息窗口
*	CString&	strVideoPathName	-- 视频文件全路径名
*	CString&	strGameType			-- 比赛类型(soccer, tableteenis,...,关系门限设定)
*	CString&	strLogoTemplPathName-- Logo模板图像文件名(与iplLogoTempl互补使用) 
*	IplImage*	iplLogoTempl		-- Logo模板图像
*	DWORD		dwVideoFrmTotal		-- 视频长度(帧)
*   double		dfVideoTimeTotal	-- 视频长度(时间秒)
*	double		dfFrameRate			-- 帧率(frame per second)
*   double		dfTimePerFrm		-- 帧长(second per frame)
*   long		lVidStrmIdx			-- 视频流在多媒体流中的索引值
*	int			nVideoHeight		-- 视频帧高度
*   int			nVideoWidth			-- 视频帧高度
*	long		lImgBufSize			-- 帧图像DIB的直接大小
*
*	1. WM_IMG_DISPLAY	当前帧数据消息	
*		wParam = 0, lParam = 0	==>	开始处理
*		wParam !=0, lParam >=0	==> 发送数据, wParam 为当前帧数据DIB, lParam 为帧号
*		wParam = 0, lParam = 1 ==> 处理结束
*
*   2. WM_LOGO_DET		Logo标志检测消息定义 
*		wParam = 0, lParam = 0	==>	开始检测
*		wParam !=0, lParam >=0	==> 发送检测结果, wParam 为候数据DIB, lParam 为帧号
*		wParam = 0, lParam = 1  ==> 检测结束
*/
bool CSMRLogoDet::SMRLogoDet(HWND hWnd, CString& strVideoPathName, CString& strGameType,
							CString& strLogoTemplPathName, /* char* pLogoTemplBuf, */
							DWORD dwVideoFrmTotal, double dfVideoTimeTotal, 
							double dfFrameRate, double dfTimePerFrm, long lVidStrmIdx,
							int nVideoHeight, int nVideoWidth, long	lImgBufSize)
{
	USES_CONVERSION;

	WIN32_FIND_DATA FindFile;
	if( ::FindFirstFile((LPCTSTR)strLogoTemplPathName, &FindFile) == INVALID_HANDLE_VALUE ) {
		AfxMessageBox("ERROR! Video File DO NOT Exist.", MB_OK);
		return false;
	}	

	char *pLogoTemplBuf = NULL;
	bool bRead = ReadDIBFile((LPTSTR)(LPCTSTR)strLogoTemplPathName, &pLogoTemplBuf);
	if(!bRead) {
		AfxMessageBox("ERROR! Please Give a Logo Templat Firstly.", MB_OK);
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
    bool bVideoFound = false;
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
   
	// 第一次运行前清除数据 ========================================================
	CRect	logoRect;	// logo 区域 (60, 15, 300, 215);	
	double margin_ratio = 1.0/7;
	logoRect.left	= (long)(nVideoWidth*margin_ratio);
	logoRect.right	= (long)(nVideoWidth*(1-margin_ratio));
	logoRect.top	= (long)(nVideoHeight * margin_ratio);
	logoRect.bottom	= (long)(nVideoHeight * (1-margin_ratio));
	
	int th_wipe_frm_diff	= 2000;		// 描述wipe过程中帧间差的门限	
	int th_wipe_noise		= 2;		// wipe 过程中容许的噪声(个别帧间差小于th_wipe_frm_diff)
	double th_logo_diff		= 45.0;		// Logo匹配的最小差
	// for SOCCER_LOGO

	if( strGameType.CompareNoCase("soccer") == 0 ) {	

		th_wipe_frm_diff	= 2000;		// 描述wipe过程中帧间差的门限	
		th_wipe_noise		= 2;		// wipe 过程中容许的噪声(个别帧间差小于th_wipe_frm_diff)

		#ifdef HSV_COLOR
			th_logo_diff	= 25.0;
		#else
			th_logo_diff	= 45.0;	// 如果采用权值差,则为35.0
		#endif
	}
	// for TABLETENNIS 
	else if( strGameType.CompareNoCase("tabletennis") == 0 ) {

		th_wipe_frm_diff	= 1000;		// 描述wipe过程中帧间差的门限	
		th_wipe_noise		= 3;		// wipe 过程中容许的噪声(个别帧间差小于th_wipe_frm_diff)

		#ifdef HSV_COLOR
			th_logo_diff	= 24.0;			// ???
		#else
			th_logo_diff	= 31.0;	// 如果采用权值差,则为30.0
		#endif
	}

	int	   th_min_SMR_dur_time = 2;	// SMR最小的延续时间
	int    th_frm_step = (int)(th_min_SMR_dur_time*dfFrameRate);
	int    nStatFrmNum = 21;	// 统计数组的大小,多少个连续的差作比较

	double th_moment = 0.25;
	
	int	th_wipe_dur_frm_min	= 10;		// wipe 延续的最小长度(14 is more accurate)
	int	th_wipe_dur_frm_max	= 20;		// wipe 延续的最大长度(一般不超过20,据观察)
	int th_wipe_logo		= 3;		// 利用wipe检测logo时需要候选wipe的最小个数

	double dStreamTimeBias = 0.0;		// 偏离检测到的wipe中心的时间
	int logo_bias_frm = 0;	
	int wipe_bias_frm = 5;		// 在wipe中心图像左右偏移wipe_bias_frm的范围内选取中心图像// 偏离距离(以帧为单位)
	BOOL bCandLogo = FALSE;		// 候选logo的有效标志
	int logo_bias = 0;

	BOOL	bWipeFlag = FALSE;	// wipe检测标志(TRUE时,检测进行中;FALSE未进行检测)
	DWORD	dwWipeStart, dwWipeEnd, dwWipePosFrm, dwLogoFrmNum;
	double  dfWipePosTime;
	int		nWipe_noise_count = 0;

	INVAR_MOMENT	Moment_Real;	// 实际图像的不变矩
	INVAR_MOMENT	Moment_Temp;	// 模板图像的不变矩
	InVariantMoment((BYTE *)pLogoTemplBuf, Moment_Temp);

	int BmpWidthBytes = (nVideoWidth*24+31)/32*4;

	char *pImgBuffer = new char[lImgBufSize];
	if (!pImgBuffer)  {
		AfxMessageBox("ERROR! Failed to allocate memory.", MB_OK);
		return false;
	}

	// 用于比较相邻两帧图像的差的空间
	BYTE *pPreImgData = new BYTE[lImgBufSize-sizeof(BITMAPINFOHEADER)];  ////////
	BYTE *pCurImgData = new BYTE[lImgBufSize-sizeof(BITMAPINFOHEADER)];  ////////
	BYTE *pLogoCandBuf= new BYTE[lImgBufSize];		// 用于保存候选logo的空间,便于多级验证

	int diff_red, diff_blue, diff_green;
	int r,c;
	double dfFrmDiff = 0.0;
	DWORD  dwFrmDiff = 0;
	double minDist = 0.0;
	int minDistIndex = 0;
	int nLogoCount = 0;		// 总共检测到的logo的个数
	int nFrameStep = 0;	// 检测到一个logo后跳跃的距离
	// logo满足 1. 在邻域差中是最小的; 2. 该差小于特定门限	
	
	// 开始处理,通知调用窗口
	SendMessage(hWnd, WM_IMG_DISPLAY, 0, 0);
	SendMessage(hWnd, WM_LOGO_DET, 0, 0);
	
	double dStreamTime = 0.0;
	for(m_dwCurFrmNo = 0; m_dwCurFrmNo < dwVideoFrmTotal; m_dwCurFrmNo++) {	

		if( m_dwCurFrmNo == 940 ) {
			int jjj = 0;
		}
	
		try {
			hr = pDet->GetBitmapBits(dStreamTime, 0, pImgBuffer, nVideoWidth, nVideoHeight);
		}
		catch (...) {
			delete [] pImgBuffer;
			AfxMessageBox("ERROR! IMediaSeeking failed to get BitmapBits.", MB_OK);
			return false;
		}
		if (SUCCEEDED(hr))	{
			
			// 发送当前处理图像数据
			SendMessage(hWnd, WM_IMG_DISPLAY, (WPARAM)pImgBuffer, m_dwCurFrmNo);

			//BYTE *pImgData = (BYTE *)(m_pImgBuffer + sizeof(BITMAPINFOHEADER));
			memcpy(pCurImgData, (BYTE *)(pImgBuffer+sizeof(BITMAPINFOHEADER)), lImgBufSize-sizeof(BITMAPINFOHEADER));
			// sizeof(m_pImgBuffer);
			if(m_dwCurFrmNo == 0) {
				memcpy(pPreImgData, pCurImgData, lImgBufSize-sizeof(BITMAPINFOHEADER));			
			}			
			
			dwFrmDiff = 0;
			for(r = nVideoHeight-1; r >= 0; r--) {
				for(c = 0; c < nVideoWidth; c++) {
					diff_blue  = (BYTE)(*(pCurImgData + r*BmpWidthBytes + 3*c)) - (BYTE)(*(pPreImgData + r*BmpWidthBytes + 3*c));
					diff_green = (BYTE)(*(pCurImgData + r*BmpWidthBytes + 3*c+1)) - (BYTE)(*(pPreImgData + r*BmpWidthBytes + 3*c+1));
					diff_red   = (BYTE)(*(pCurImgData + r*BmpWidthBytes + 3*c+2)) - (BYTE)(*(pPreImgData + r*BmpWidthBytes + 3*c+2));

					dwFrmDiff = dwFrmDiff + (diff_blue*diff_blue) + (diff_green*diff_green) + (diff_red*diff_red);			

				}
			}
			dwFrmDiff = (int)( dwFrmDiff / (3*nVideoHeight*nVideoWidth) );

			if(dwFrmDiff >= (DWORD)th_wipe_frm_diff && bWipeFlag == FALSE) {
				dwWipeStart = m_dwCurFrmNo;
				bWipeFlag   = TRUE;				// wipe检测开始
			}
			if( bWipeFlag ) {
				if( dwFrmDiff >= (DWORD)th_wipe_frm_diff ) {
					dwWipeEnd = m_dwCurFrmNo;
					nWipe_noise_count = 0;
				}
				else {
					nWipe_noise_count++;
				}
				// 如果超过噪声容限或者视频结束,则需要作出判断
				if( nWipe_noise_count >= th_wipe_noise || m_dwCurFrmNo == dwVideoFrmTotal-1 ) {
					// 判断wipe是否有效
					if( (int)(dwWipeEnd - dwWipeStart) >= th_wipe_dur_frm_min /* && (int)(dwWipeEnd - dwWipeStart) <= th_wipe_dur_frm_max */) {	// valid
						// 起点取wipe的起点,遍历整个wipe区间
						dwWipePosFrm = dwWipeStart;	// (dwWipeStart + dwWipeEnd) / 2;
						dfWipePosTime = dwWipePosFrm * dfVideoTimeTotal / dwVideoFrmTotal;
						
						// 计算该序列中与LogoTempl距离最小的帧,并保留该号
						//minDist = 256.0; minDistIndex = -wipe_bias_frm;
						//for(logo_bias = -wipe_bias_frm; logo_bias <= wipe_bias_frm; logo_bias++) 
						minDist = 256.0; minDistIndex = 0;
						for(logo_bias = 0; logo_bias <= (int)(dwWipeEnd - dwWipeStart); logo_bias++) 
						{
							// 取出近邻图像
							dStreamTimeBias = dfWipePosTime + logo_bias*dfTimePerFrm;
							try {
								hr = pDet->GetBitmapBits(dStreamTimeBias, 0, pImgBuffer, nVideoWidth, nVideoHeight);
							}
							catch (...) {
								delete [] pImgBuffer;
								AfxMessageBox("ERROR! IMediaSeeking failed to get BitmapBits.", MB_OK);
								return false;
							}
							if (SUCCEEDED(hr))	{
								// 计算logo中心区域的差 (直接差或者加权差)
								#ifdef HSV_COLOR
									// HSV 颜色空间测颜色差 =============================								
									//dfFrmDiff = ImgColorDiff(logoImg, CurIplImg, "HSV");pLogoTemplBuf
									dfFrmDiff = ImgColorDiff(pLogoTemplBuf, pImgBuffer, "HSV");
								#else
									// RGB颜色空间的亮度差 ==============================
									//dfFrmDiff = ImgColorDiff(logoImg, CurIplImg, "RGB");								
									dfFrmDiff = ImgColorDiff(pLogoTemplBuf, pImgBuffer, "RGB");								
								#endif
								
								/*
								dfFrmDiff = 0;
								//double d_x=0, d_y=0, dist_pt=0.0;
								double weight=0.0;	 
								int nDiff = 0;
								for(int rr = 0; rr < m_lHeight; rr++) {
									for(int cc = 0; cc < m_lWidth; cc++) {
										//d_x = cc - m_lWidth/2;
										//d_y = rr - m_lHeight/2;
            
										//dist_pt = (d_x*d_x)/( (logoRect.Width()/2) * (logoRect.Width()/2) ) + (d_y*d_y) / ( (logoRect.Height()/2) * (logoRect.Height()/2) );
										//if( dist_pt >= 1.0 )
										//	continue;
										//weight = 1 - 4*dist_pt/(2*PI);
										weight = 1.0;

										// 注意像素点的对应
										diff_blue  = (BYTE)(*(logoImg->imageData + rr*logoImg->widthStep + 3*cc)) - (BYTE)(*(m_pImgBuffer+sizeof(BITMAPINFOHEADER) + (m_lHeight-1-rr)*BmpWidthBytes + 3*cc));
										diff_green = (BYTE)(*(logoImg->imageData + rr*logoImg->widthStep + 3*cc+1)) - (BYTE)(*(m_pImgBuffer+sizeof(BITMAPINFOHEADER) + (m_lHeight-1-rr)*BmpWidthBytes + 3*cc+1));
										diff_red   = (BYTE)(*(logoImg->imageData + rr*logoImg->widthStep + 3*cc+2)) - (BYTE)(*(m_pImgBuffer+sizeof(BITMAPINFOHEADER) + (m_lHeight-1-rr)*BmpWidthBytes + 3*cc+2));

										nDiff = abs(diff_blue) + abs(diff_green) + abs(diff_red);

										dfFrmDiff = dfFrmDiff + nDiff*weight;
									}
								}
								dfFrmDiff = dfFrmDiff / (3*m_lHeight*m_lWidth);
								*/
								if(minDist > dfFrmDiff) {
									minDist = dfFrmDiff;
									minDistIndex = logo_bias;
									// 保存当前帧数据,已备继续验证
									memcpy(pLogoCandBuf, pImgBuffer, lImgBufSize);
								}
							}
						}	// end of (寻找最小的帧间差)

						// 如果最小差小于一个绝对阈值,则OK,否则不行						
						if( minDist < th_logo_diff ) {
							// 进一步用不变矩检测验证标志图
							InVariantMoment(pLogoCandBuf, Moment_Real);						
							if( fabs(Moment_Temp.phi_1 - Moment_Real.phi_1) / Moment_Temp.phi_1 < th_moment ) {
								// 如果找到logo,则记录这个位置								
								nLogoCount++;

								dwLogoFrmNum = dwWipePosFrm + minDistIndex;
								// 发送消息,显示并记录该图像
								SendMessage(hWnd, WM_LOGO_DET, (WPARAM)pLogoCandBuf, dwLogoFrmNum);

								//strData.Format("%d \t %d\r\n", nLogoCount, dwWipePosFrm + minDistIndex);
								//WriteFile(hFile, strData, strData.GetLength(), &dwWriteBytes, NULL);
								
								nFrameStep = 0;	// 开始设置跳跃帧,在后面的一个小范围内不再检测
							}
							
						}
					}	// end of (有效的 logo trans)
					else {	// invalid wipe, clear data
						dwWipeStart = 0;
						dwWipeEnd	= 0;
					}

					nWipe_noise_count = 0;
					bWipeFlag = false;
					bCandLogo = false;
				}	// end of (判断是否有效的logo trans)
			}	// end of (logo trans 判断)

			//==数据更新============
			//cvCopyImage(m_CurIplImg, m_PreIplImg);	
			memcpy(pPreImgData, pCurImgData, lImgBufSize-sizeof(BITMAPINFOHEADER));

		}    // end of (成功提取一帧后的处理)

		dStreamTime += dfTimePerFrm;
	}

	delete []pPreImgData;
	delete []pCurImgData;
	delete []pLogoCandBuf;
	delete []pImgBuffer;
	
	if(pLogoTemplBuf != NULL) {
		delete []pLogoTemplBuf;
	}
	pLogoTemplBuf = NULL;

	SendMessage(hWnd, WM_IMG_DISPLAY, 0, 1);	// 检测结束
	SendMessage(hWnd, WM_LOGO_DET, 0, 1);	// 检测结束

	return true;
}

/* 
*	利用回放标志模板提取整场比赛中的回放标志, 利用单帧图像匹配
*	
*	方法：Image matching
*	step 1 : 颜色差
*	step 2 : 比较不变矩
*			
*
*	参数说明:
*	HWND		hWnd				-- 接受结果消息窗口
*	CString&	strVideoPathName	-- 视频文件全路径名
*	CString&	strGameType			-- 比赛类型(soccer, tableteenis,...,关系门限设定)
*	CString&	strLogoTemplPathName-- Logo模板图像文件名(与iplLogoTempl互补使用) 
*	IplImage*	iplLogoTempl		-- Logo模板图像
*	DWORD		dwVideoFrmTotal		-- 视频长度(帧)
*   double		dfVideoTimeTotal	-- 视频长度(时间秒)
*	double		dfFrameRate			-- 帧率(frame per second)
*   double		dfTimePerFrm		-- 帧长(second per frame)
*   long		lVidStrmIdx			-- 视频流在多媒体流中的索引值
*	int			nVideoHeight		-- 视频帧高度
*   int			nVideoWidth			-- 视频帧高度
*	long		lImgBufSize			-- 帧图像DIB的直接大小
*
*	1. WM_IMG_DISPLAY	当前帧数据消息	
*		wParam = 0, lParam = 0	==>	开始处理
*		wParam !=0, lParam >=0	==> 发送数据, wParam 为当前帧数据DIB, lParam 为帧号
*		wParam = 0, lParam = 1 ==> 处理结束
*
*   2. WM_LOGO_DET		Logo标志检测消息定义 
*		wParam = 0, lParam = 0	==>	开始检测
*		wParam !=0, lParam >=0	==> 发送检测结果, wParam 为候数据DIB, lParam 为帧号
*		wParam = 0, lParam = 1  ==> 检测结束
*/
bool CSMRLogoDet::SMRLogoDet_ImgMatch(HWND hWnd, CString& strVideoPathName, CString& strGameType,
							CString& strLogoTemplPathName, /* char* pLogoTemplBuf, */
							DWORD dwVideoFrmTotal, double dfVideoTimeTotal, 
							double dfFrameRate, double dfTimePerFrm, long lVidStrmIdx,
							int nVideoHeight, int nVideoWidth, long	lImgBufSize)
{
	USES_CONVERSION;

	WIN32_FIND_DATA FindFile;
	if( ::FindFirstFile((LPCTSTR)strLogoTemplPathName, &FindFile) == INVALID_HANDLE_VALUE ) {
		AfxMessageBox("ERROR! Video File DO NOT Exist.", MB_OK);
		return false;
	}	

	char *pLogoTemplBuf = NULL;
	bool bRead = ReadDIBFile((LPTSTR)(LPCTSTR)strLogoTemplPathName, &pLogoTemplBuf);
	if(!bRead) {
		AfxMessageBox("ERROR! Please Give a Logo Templat Firstly.", MB_OK);
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
    bool bVideoFound = false;
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
   
	// 第一次运行前清除数据 ========================================================
	CRect	logoRect;	// logo 区域 (60, 15, 300, 215);	
	double margin_ratio = 1.0/7;
	logoRect.left	= (long)(nVideoWidth*margin_ratio);
	logoRect.right	= (long)(nVideoWidth*(1-margin_ratio));
	logoRect.top	= (long)(nVideoHeight * margin_ratio);
	logoRect.bottom	= (long)(nVideoHeight * (1-margin_ratio));

	double th_logo_diff	= 45.0;		// Logo匹配的最小差
	double th_moment	= 0.25;	
	// for SOCCER_LOGO

	// local windows width = 21
	DWORD	dwCurFrmIdx;	// current frame index in the local array
	DWORD	dwCentFrmIdx;	// current center frame index in the local array
	DWORD	dwLogoFrmNo;

	int i;

	int		nWinSize = 21;
	double*	arLocalDiff = NULL;
	arLocalDiff = new double[nWinSize];
	//memset(arLocalDiff, 256, nWinSize*sizeof(double));
	for(i = 0; i < nWinSize; i++) {
		arLocalDiff[i] = 256.0;
	}

	if( strGameType.CompareNoCase("soccer") == 0 ) {	

		#ifdef HSV_COLOR
			th_logo_diff	= 25.0;
		#else
			th_logo_diff	= 45.0;	// 如果采用权值差,则为35.0
		#endif
	}
	// for TABLETENNIS 
	else if( strGameType.CompareNoCase("tabletennis") == 0 ) {

		#ifdef HSV_COLOR
			th_logo_diff	= 24.0;			// ???
		#else
			th_logo_diff	= 31.0;	// 如果采用权值差,则为30.0
		#endif
	}

	int    nStatFrmNum = 21;	// 统计数组的大小,多少个连续的差作比较

	double dStreamTimeBias = 0.0;		// 偏离检测到的wipe中心的时间

	BOOL bCandLogo = FALSE;		// 候选logo的有效标志
	int logo_bias = 0;

	INVAR_MOMENT	Moment_Real;	// 实际图像的不变矩
	INVAR_MOMENT	Moment_Temp;	// 模板图像的不变矩
	InVariantMoment((BYTE *)pLogoTemplBuf, Moment_Temp);

	int BmpWidthBytes = (nVideoWidth*24+31)/32*4;

	char *pImgBuffer = new char[lImgBufSize];
	if (!pImgBuffer)  {
		AfxMessageBox("ERROR! Failed to allocate memory.", MB_OK);
		return false;
	}

	// 用于比较相邻两帧图像的差的空间
	BYTE *pLogoCandBuf= new BYTE[lImgBufSize];		// 用于保存候选logo的空间,便于多级验证

	double dfFrmDiff = 0.0;
	DWORD  dwFrmDiff = 0;
	double minDist = 0.0;
	int minDistIndex = 0;
	int nLogoCount = 0;		// 总共检测到的logo的个数
	int nFrameStep = 0;	// 检测到一个logo后跳跃的距离
	// logo满足 1. 在邻域差中是最小的; 2. 该差小于特定门限	
	
	// 开始处理,通知调用窗口
	SendMessage(hWnd, WM_IMG_DISPLAY, 0, 0);
	SendMessage(hWnd, WM_LOGO_DET, 0, 0);
	
	double dStreamTime = 0.0;
	for(m_dwCurFrmNo = 0; m_dwCurFrmNo < dwVideoFrmTotal; m_dwCurFrmNo++) {	
	
		try {
			hr = pDet->GetBitmapBits(dStreamTime, 0, pImgBuffer, nVideoWidth, nVideoHeight);
		}
		catch (...) {
			delete [] pImgBuffer;
			AfxMessageBox("ERROR! IMediaSeeking failed to get BitmapBits.", MB_OK);
			return false;
		}
		if (SUCCEEDED(hr))	{
			
			// 发送当前处理图像数据
			SendMessage(hWnd, WM_IMG_DISPLAY, (WPARAM)pImgBuffer, m_dwCurFrmNo);
				
			// 计算logo中心区域的差 (直接差或者加权差)
			#ifdef HSV_COLOR
				// HSV 颜色空间测颜色差 =============================								
				//dfFrmDiff = ImgColorDiff(logoImg, CurIplImg, "HSV");pLogoTemplBuf
				dfFrmDiff = ImgColorDiff(pLogoTemplBuf, pImgBuffer, "HSV");
			#else
				// RGB颜色空间的亮度差 ==============================
				//dfFrmDiff = ImgColorDiff(logoImg, CurIplImg, "RGB");								
				dfFrmDiff = ImgColorDiff(pLogoTemplBuf, pImgBuffer, "RGB");								
			#endif

			// 放入数组,并寻找最小差值以及其index, 需要考察的是中间值				
			dwCurFrmIdx  = m_dwCurFrmNo % nWinSize;
			dwCentFrmIdx = (m_dwCurFrmNo - nWinSize/2 + nWinSize) % nWinSize;

			arLocalDiff[dwCurFrmIdx] = dfFrmDiff;
			

			// step 1: 当前中心值是否在数组中是最小的
			bool bIsMin = true;
			for(i = 0; i < nWinSize; i++) {
				if( arLocalDiff[dwCentFrmIdx] > arLocalDiff[i] ) {
					bIsMin = false;
					break;
				}
			}

			if( bIsMin == true )  {
				// 如果最小差小于一个绝对阈值,则OK,否则不行						
				if( arLocalDiff[dwCentFrmIdx] < th_logo_diff ) {

					double dfLogoTime = dStreamTime - nWinSize/2 * dfTimePerFrm;
					try {
						hr = pDet->GetBitmapBits(dfLogoTime, 0, (char *)pLogoCandBuf, nVideoWidth, nVideoHeight);
					}
					catch (...) {
						delete [] pLogoCandBuf;
						AfxMessageBox("ERROR! IMediaSeeking failed to get BitmapBits.", MB_OK);
						return false;
					}

					if (SUCCEEDED(hr))	{					
						// 进一步用不变矩检测验证标志图
						InVariantMoment(pLogoCandBuf, Moment_Real);						
						if( fabs(Moment_Temp.phi_1 - Moment_Real.phi_1) / Moment_Temp.phi_1 < th_moment ) {
						
							dwLogoFrmNo = m_dwCurFrmNo - nWinSize/2;

							// 发送消息,显示并记录该图像
							SendMessage(hWnd, WM_LOGO_DET, (WPARAM)pLogoCandBuf, dwLogoFrmNo);
							
							//strData.Format("%d \t %d\r\n", nLogoCount, dwWipePosFrm + minDistIndex);
							//WriteFile(hFile, strData, strData.GetLength(), &dwWriteBytes, NULL);						
						}					
					}	
				} // end of if(diff < th)
			} // end of if(min)
			
		}    // end of (成功提取一帧后的处理)

		dStreamTime += dfTimePerFrm;
	}

	delete []pLogoCandBuf;
	delete []pImgBuffer;
	delete []arLocalDiff;
	
	if(pLogoTemplBuf != NULL) {
		delete []pLogoTemplBuf;
	}
	pLogoTemplBuf = NULL;

	SendMessage(hWnd, WM_IMG_DISPLAY, 0, 1);	// 检测结束
	SendMessage(hWnd, WM_LOGO_DET, 0, 1);	// 检测结束

	return true;
}

// 对DIB数据求矩
void CSMRLogoDet::InVariantMoment(BYTE* pImgDIB, INVAR_MOMENT& moment)
{
	BITMAPINFOHEADER *biHead = NULL;
	biHead = (BITMAPINFOHEADER *)pImgDIB;
	
	int nHeight = biHead->biHeight;
	int nWidth  = biHead->biWidth;

	int BmpWidthBytes = (nWidth*24+31)/32*4;

	BYTE red, green, blue;
	int  gry;

	// 计算两幅图像的矩特征	
	DWORD m_00 = 0, m_10 = 0, m_01 = 0;
	// center
	int x_cent, y_cent;	
	double mu_20 = 0.0, eta_20 = 0.0;
	double mu_02 = 0.0, eta_02 = 0.0;
	double mu_11 = 0.0, eta_11 = 0.0;
	double mu_30 = 0.0, eta_30 = 0.0;
	double mu_03 = 0.0, eta_03 = 0.0;
	double mu_12 = 0.0,	eta_12 = 0.0;
	double mu_21 = 0.0,	eta_21 = 0.0;

	int i, j;

	m_00 = 0; m_10 = 0; m_01 = 0;
	for(i=0; i < nHeight; i++) {
		for(j=0; j < nWidth; j++) {
			blue	= (BYTE)(*(pImgDIB+sizeof(BITMAPINFOHEADER)+ i*BmpWidthBytes + 3*j));
			green	= (BYTE)(*(pImgDIB+sizeof(BITMAPINFOHEADER)+ i*BmpWidthBytes + 3*j+1));
			red		= (BYTE)(*(pImgDIB+sizeof(BITMAPINFOHEADER)+ i*BmpWidthBytes + 3*j+2));
			gry	    = (3*red + 6*green + blue) / 10;

			m_00 = m_00 + gry;
			m_10 = m_10 + j*gry;
			m_01 = m_01 + i*gry;
		}
	}

	x_cent = m_10/m_00;
	y_cent = m_01/m_00;

	for(i=0; i < nHeight; i++) {
		for(j=0; j < nWidth; j++) {

			blue	= (BYTE)(*(pImgDIB+sizeof(BITMAPINFOHEADER)+ i*BmpWidthBytes + 3*j));
			green	= (BYTE)(*(pImgDIB+sizeof(BITMAPINFOHEADER)+ i*BmpWidthBytes + 3*j+1));
			red		= (BYTE)(*(pImgDIB+sizeof(BITMAPINFOHEADER)+ i*BmpWidthBytes + 3*j+2));
			gry	    = (3*red + 6*green + blue) / 10;

			mu_20 = mu_20 + pow((j-x_cent), 2) * gry;
			mu_02 = mu_02 + pow((i-y_cent), 2) * gry;
			mu_11 = mu_11 + (j-x_cent)*(i-y_cent) * gry;
			mu_30 = mu_30 + pow((j-x_cent), 3) * gry;
			mu_03 = mu_03 + pow((i-y_cent), 3) * gry;
			mu_12 = mu_12 + (j-x_cent)*pow(i-y_cent,2) * gry;
			mu_21 = mu_21 + pow(j-x_cent,2)*(i-y_cent) * gry;
		}
	}

	eta_20 = mu_20 / (pow(m_00, 2));
	eta_02 = mu_02 / (pow(m_00, 2));
	eta_11 = mu_11 / (pow(m_00, 2));
	eta_30 = mu_30 / (pow(m_00, 2.5));
	eta_03 = mu_03 / (pow(m_00, 2.5));
	eta_12 = mu_12 / (pow(m_00, 2.5));
	eta_21 = mu_21 / (pow(m_00, 2.5));

	moment.x_cent = x_cent;
	moment.y_cent = y_cent;
	moment.phi_1 = eta_20 + eta_02;
	moment.phi_2 = (eta_20-eta_02)*(eta_20-eta_02) + 4*eta_11*eta_11;
	moment.phi_3 = (eta_30-3*eta_12)*(eta_30-3*eta_12) + (3*eta_21-eta_03)*(3*eta_21-eta_03);
	moment.phi_4 = (eta_30+eta_12)*(eta_30+eta_12) + (eta_21+eta_03)*(eta_21+eta_03);

}

void CSMRLogoDet::InVariantMoment(IplImage* img, INVAR_MOMENT& moment)
{
	int nHeight = img->height;
	int nWidth  = img->width;

	IplImage* gryImg = cvCreateImage(cvSize(nWidth, nHeight), 8, 1);
	cvCvtColor(img, gryImg, CV_RGB2GRAY);

	// 计算两幅图像的矩特征	
	DWORD m_00 = 0, m_10 = 0, m_01 = 0;
	// center
	int x_cent, y_cent;	
	double mu_20 = 0.0, eta_20 = 0.0;
	double mu_02 = 0.0, eta_02 = 0.0;
	double mu_11 = 0.0, eta_11 = 0.0;
	double mu_30 = 0.0, eta_30 = 0.0;
	double mu_03 = 0.0, eta_03 = 0.0;
	double mu_12 = 0.0,	eta_12 = 0.0;
	double mu_21 = 0.0,	eta_21 = 0.0;

	int i, j;

	m_00 = 0; m_10 = 0; m_01 = 0;
	for(i=0; i < nHeight; i++) {
		for(j=0; j < nWidth; j++) {
			m_00 = m_00 + (BYTE)(gryImg->imageData[i*gryImg->widthStep+j]);
			m_10 = m_10 + j*(BYTE)(gryImg->imageData[i*gryImg->widthStep+j]);
			m_01 = m_01 + i*(BYTE)(gryImg->imageData[i*gryImg->widthStep+j]);
		}
	}

	x_cent = m_10/m_00;
	y_cent = m_01/m_00;

	for(i=0; i < nHeight; i++) {
		for(j=0; j < nWidth; j++) {
			mu_20 = mu_20 + pow((j-x_cent), 2)*(BYTE)(gryImg->imageData[i*gryImg->widthStep+j]);
			mu_02 = mu_02 + pow((i-y_cent), 2)*(BYTE)(gryImg->imageData[i*gryImg->widthStep+j]);
			mu_11 = mu_11 + (j-x_cent)*(i-y_cent)*(BYTE)(gryImg->imageData[i*gryImg->widthStep+j]);
			mu_30 = mu_30 + pow((j-x_cent), 3)*(BYTE)(gryImg->imageData[i*gryImg->widthStep+j]);
			mu_03 = mu_03 + pow((i-y_cent), 3)*(BYTE)(gryImg->imageData[i*gryImg->widthStep+j]);
			mu_12 = mu_12 + (j-x_cent)*pow(i-y_cent,2)*(BYTE)(gryImg->imageData[i*gryImg->widthStep+j]);
			mu_21 = mu_21 + pow(j-x_cent,2)*(i-y_cent)*(BYTE)(gryImg->imageData[i*gryImg->widthStep+j]);
		}
	}

	eta_20 = mu_20 / (pow(m_00, 2));
	eta_02 = mu_02 / (pow(m_00, 2));
	eta_11 = mu_11 / (pow(m_00, 2));
	eta_30 = mu_30 / (pow(m_00, 2.5));
	eta_03 = mu_03 / (pow(m_00, 2.5));
	eta_12 = mu_12 / (pow(m_00, 2.5));
	eta_21 = mu_21 / (pow(m_00, 2.5));

	moment.x_cent = x_cent;
	moment.y_cent = y_cent;
	moment.phi_1 = eta_20 + eta_02;
	moment.phi_2 = (eta_20-eta_02)*(eta_20-eta_02) + 4*eta_11*eta_11;
	moment.phi_3 = (eta_30-3*eta_12)*(eta_30-3*eta_12) + (3*eta_21-eta_03)*(3*eta_21-eta_03);
	moment.phi_4 = (eta_30+eta_12)*(eta_30+eta_12) + (eta_21+eta_03)*(eta_21+eta_03);
	
	cvReleaseImage(&gryImg);
}

// 计算两幅图像的对应点的差,可以选择不同颜色空间
double CSMRLogoDet::ImgColorDiff(char* pRGBDIB1, char* pRGBDIB2, const char *strColor)
{
	int r,c;
	BITMAPINFOHEADER* pBIH = (BITMAPINFOHEADER*)pRGBDIB1;

	int nHeight = pBIH->biHeight;
	int nWidth  = pBIH->biWidth;

	double dfFrmDiff = 0.0;	

	if( 0 == strcmp(strColor, "RGB") ) {
		RGBTRIPLE* pRGB1 = (RGBTRIPLE*)( pRGBDIB1 + sizeof(BITMAPINFOHEADER) );
		RGBTRIPLE* pRGB2 = (RGBTRIPLE*)( pRGBDIB2 + sizeof(BITMAPINFOHEADER) );

		int diff_blue, diff_green, diff_red;
		for(r = 0; r < nHeight; r++) {
			for(c = 0; c < nWidth; c++) {

				// 注意像素点的对应
				diff_red   = pRGB1->rgbtRed - pRGB2->rgbtRed;
				diff_green = pRGB1->rgbtGreen - pRGB2->rgbtGreen;
				diff_blue  = pRGB1->rgbtBlue - pRGB2->rgbtBlue;
				/*
				diff_blue  = (BYTE)(*(RGBImg1->imageData + r*RGBImg1->widthStep + 3*c)) - (BYTE)(*(RGBImg2->imageData + r*RGBImg2->widthStep + 3*c));
				diff_green = (BYTE)(*(RGBImg1->imageData + r*RGBImg1->widthStep + 3*c+1)) - (BYTE)(*(RGBImg2->imageData + r*RGBImg2->widthStep + 3*c+1));
				diff_red   = (BYTE)(*(RGBImg1->imageData + r*RGBImg1->widthStep + 3*c+2)) - (BYTE)(*(RGBImg2->imageData + r*RGBImg2->widthStep + 3*c+2));
				*/
				dfFrmDiff = dfFrmDiff + abs(diff_blue) + abs(diff_green) + abs(diff_red);
				pRGB1++;
				pRGB2++;
			}
		}
		dfFrmDiff = dfFrmDiff / (3*nHeight*nWidth);
	}
	else if( 0 == strcmp(strColor, "HSV") ) {
		IplImage *RGBImg1 = ImgBufToiplImage(pRGBDIB1);
		IplImage *RGBImg2 = ImgBufToiplImage(pRGBDIB2);

		IplImage *HSVImg1 = NULL;	// HSV 色彩图像,由rgb色彩转换
		HSVImg1 = iplCloneImage(RGBImg1);
		iplRGB2HSV(RGBImg1, HSVImg1);
		IplImage *HSVImg2 = NULL;	// HSV 色彩图像,由rgb色彩转换
		HSVImg2 = iplCloneImage(RGBImg2);
		iplRGB2HSV(RGBImg2, HSVImg2);

		double Hue1=0.0, Sat1=0.0, Val1=0.0;
		double Hue2=0.0, Sat2=0.0, Val2=0.0;
		double theta = 0.0, dist_val = 0.0, dist_chr = 0.0, dist_hsv = 0.0;

		for(r = 0; r < nHeight; r++) {
			for(c = 0; c < nWidth; c++) {

				Hue1 = ( (BYTE)(*(HSVImg1->imageData + r*HSVImg1->widthStep + 3*c)) ) / 255.0;
				Sat1 = ( (BYTE)(*(HSVImg1->imageData + r*HSVImg1->widthStep + 3*c + 1)) ) / 1.0;
				Val1 = ( (BYTE)(*(HSVImg1->imageData + r*HSVImg1->widthStep + 3*c + 2)) ) / 1.0;
			
				Hue2 = ( (BYTE)(*(HSVImg2->imageData + r*HSVImg2->widthStep + 3*c)) ) / 255.0;
				Sat2 = ( (BYTE)(*(HSVImg2->imageData + r*HSVImg2->widthStep + 3*c + 1)) ) / 1.0;
				Val2 = ( (BYTE)(*(HSVImg2->imageData + r*HSVImg2->widthStep + 3*c + 2)) ) / 1.0;

				theta = Hue1 - Hue2;
				theta = 2*3.1415926*theta; // 转化为弧度值
				dist_chr = sqrt( Sat1*Sat1 + Sat2*Sat2 - 2*Sat1*Sat2*cos(theta) );
				dist_val = Val1 - Val2;

				dist_hsv = sqrt( dist_val*dist_val + dist_chr*dist_chr );

				dfFrmDiff = dfFrmDiff + dist_hsv;
			}
		}

		dfFrmDiff = dfFrmDiff / (3*nHeight*nWidth);

		if(RGBImg1 != NULL) {
			iplDeallocate( RGBImg1, IPL_IMAGE_HEADER | IPL_IMAGE_DATA );
			RGBImg1 = NULL;
		}
		if(RGBImg2 != NULL) {
			iplDeallocate( RGBImg2, IPL_IMAGE_HEADER | IPL_IMAGE_DATA );
			RGBImg2 = NULL;
		}

		if(HSVImg1 != NULL) {
			iplDeallocate( HSVImg1, IPL_IMAGE_HEADER | IPL_IMAGE_DATA );
			HSVImg1 = NULL;
		}
		if(HSVImg2 != NULL) {
			iplDeallocate( HSVImg2, IPL_IMAGE_HEADER | IPL_IMAGE_DATA );
			HSVImg2 = NULL;
		}
	}
	else 
		dfFrmDiff = 0.0;

	return dfFrmDiff;
}