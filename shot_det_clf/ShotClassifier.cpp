// ShotClassifier.cpp: implementation of the CShotClassifier class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "SVEDet.h"
#include "ShotClassifier.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CShotClassifier::CShotClassifier()
{

}

CShotClassifier::~CShotClassifier()
{

}

/*
*	
/*
*	shot classification with a hierarchy model(tree)
*	level 1:  
*			(1)	replay;		(2) live
*	level 2: from the above level
*			(2.1) long view; (2.2) medium view; (2.3) close-up view; (2.4) audience
*	level 3: from the above level
*			(2.1.1) left goal view;	(2.1.2) middle view; (2.1.3) right goal view;;
*			(2.2.1) player following; (2.2.2) static medium view
*			(2.3.1) close-up-IF;	(2.3.2) close-up-OF
*			(2.4)   audience
*
*	Features :
*			1. frame-to-frame difference(MSD)
*			2. (1) grass-ratio; (2) texture; (3) motion activity
*			3. (1) slant angle(in long view)
*			4. optional feature (Skin detection)
*	Procedure ===
*	step 1: 从数据库中读出镜头信息(起止点,标定的类别等),注意起止处的边缘帧
*	step 2: 对每个镜头
*			2.1 处理每一帧(或间隔处理),对帧进行分类, 分类结果对类型进行投票(累积数组)
			2.2 得票最多的类作为镜头的类别
			2.3 记录每类的得票,和最终的分类结果
*	step 3: 统计整个视频中镜头分类的效果,将机诶国写入文件
*
*	Input:	a shot (video clip) assume that shots have been already segmented
*			the shot data are read from database
*	Output:	statistical features of the input shot and classification information

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
*	返回值		镜头类型的索引值( 0 ~ 8 ), 如果为 -1,表示出错
*
*
*	By Tong Xiao-feng, 2004.04.04
*/

int CShotClassifier::ShotClassification(HWND hWnd, CString& strVideoPathName, DWORD dwShotStartFrm, DWORD dwShotEndFrm, 
									  DWORD dwVideoFrmTotal, double dfVideoTimeTotal, 
									  double dfFrameRate, double dfTimePerFrm, long lVidStrmIdx,
							          int nVideoHeight, int nVideoWidth, long	lImgBufSize)
{
	// ******************************************************************************* //
	// IMediaDet Initialization

	USES_CONVERSION;

	if(strVideoPathName.IsEmpty()) {
		AfxMessageBox("No intput video file!", MB_OK);
		return -1;
	}

    HRESULT hr;

    // create a media detector
    CComPtr< IMediaDet > pDet;
	hr = CoCreateInstance( CLSID_MediaDet, NULL, CLSCTX_INPROC_SERVER, 
                           IID_IMediaDet, (void**) &pDet );
	if(FAILED(hr)) {
		AfxMessageBox(" ERROR! Failed to Create COM Instance.", MB_OK);
		return -1;
	}
 
    // set filename and look for a video stream
    long Streams = 0;
    BOOL bVideoFound = FALSE;
    hr = pDet->put_Filename( T2W( strVideoPathName ) );
	if(FAILED(hr)) {
		AfxMessageBox("ERROR! IMediaDet failed to put file name.", MB_OK);
		return -1;
	}

    hr = pDet->get_OutputStreams( &Streams );
	if(FAILED(hr)) {
		AfxMessageBox("ERROR! IMediaDet failed to get output Stream.", MB_OK);
		return -1;
	}

	// 直接对视频流存取
	hr = pDet->put_CurrentStream( lVidStrmIdx );
	if(FAILED(hr)) {
		AfxMessageBox("ERROR! IMediaDet failed to put current Stream.", MB_OK);
		return -1;
	}

    // this method will change the MediaDet to go into "sample grabbing mode" at time 0.
    hr = pDet->EnterBitmapGrabMode( 0.0 );
	if(FAILED(hr)) {
		AfxMessageBox("ERROR! IMediaDet failed to enter bitmap GrabMode.", MB_OK);
		return -1;
	}
	
    // ask for the sample grabber filter that we know lives inside the graph made by the MediaDet
    CComPtr< ISampleGrabber > pGrabber;
    hr = pDet->GetSampleGrabber( &pGrabber );
	if(FAILED(hr)) {
		AfxMessageBox("ERROR! IMediaDet failed to get Sample Grabber.", MB_OK);
		return -1;
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

	// 处理程序 ===================================================================== //
	char* pImgBuffer = new char[lImgBufSize];
	if (!pImgBuffer)  {
		AfxMessageBox("ERROR! Failed to allocate memory.", MB_OK);
		return -1;
	}
	IplImage* iplImage = NULL;

	// 类别投票箱Long(0,1,2), M(3,4), C(5,6), OF(7), OT(-1)
	int VoteBox[9];
	double	voteRatio = 0.0;	// 最佳类型的得票率
	int TrueShotType, DetShotType;	// 真实和检测到的镜头类别
	int nDetOK = 0;	// 是否检测正确的标志
	CString	strDetType = _T("");	// 镜头检测类型

	CMatrix<int> DetMatrx(4, 4);	// 检测评价矩阵,五类 (G,M,C,O)
	int MainTrueType=0, MainDetType=0;	// 主要的检测类别,五类

	// ================================================================================
	double dStreamTime = 0.0;
	DWORD	dwFrmIndex = 0;		// 图像帧的索引值(当前值减去截距)
	int		nShotIndex = 0;
	DWORD	dwFrmMargin = 0;		// 镜头起止点处的空白边缘(不处理)
	DWORD	dwProcFrmStep = 1;	// 处理帧的步长(不一定需要每帧都处理)
	DWORD	dwCurEntryIndex=0;	// 当前处理数据项的索引号
	
	memset(VoteBox, 0, sizeof(int)*9);
	voteRatio	 = 0.0;
	TrueShotType = -2;	// 镜头真实类型
	DetShotType  = -2;	// 镜头检测得到的类型
	strDetType   = _T("");
	nDetOK		 = 0;		// 是否检测正确的标志	

	dwFrmMargin = 24;	//	dwFrmMargin = 0;

	for(m_dwCurFrmNo = dwShotStartFrm+dwFrmMargin; m_dwCurFrmNo < dwShotEndFrm-dwFrmMargin; m_dwCurFrmNo+=dwProcFrmStep) 
	{
		
		dwCurEntryIndex = (m_dwCurFrmNo - dwFrmMargin) / dwProcFrmStep;	

		dStreamTime = m_dwCurFrmNo*dfTimePerFrm;
		
		try {
			hr = pDet->GetBitmapBits(dStreamTime, 0, pImgBuffer, nVideoWidth, nVideoHeight);
		}
		catch (...) {
			delete [] pImgBuffer;
			AfxMessageBox("ERROR! IMediaSeeking failed to get BitmapBits.", MB_OK);
			return -1;
		}
		if (SUCCEEDED(hr))	{
			if(iplImage != NULL) {
				iplDeallocateImage(iplImage);
				iplDeallocate(iplImage,IPL_IMAGE_ALL);
			}
			iplImage = ImgBufToiplImage(pImgBuffer);
			if(iplImage == NULL) {
				AfxMessageBox("Image Convertion Failed!", MB_OK);
				delete []pImgBuffer;
				return -1;
			}

			// 画面分类 ===================================================
			int nViewType = -1;
			nViewType = ViewClassification(iplImage);
			if(nViewType < 0)	nViewType = 8;

			VoteBox[nViewType]++;							
		} 				
	}	// end of for(FrmNo)

	// 统计得票与最佳类别
	int maxVote = 0, maxTypeIndex = 0;
	int totalVote = 0;	// 初值设为 1, 防止为 0 
	for(int i=0; i < 9; i++) {
		totalVote += VoteBox[i];
		if( maxVote < VoteBox[i] ) {
			maxVote = VoteBox[i];
			maxTypeIndex = i;
		}
	}
	if( 0 == totalVote )	voteRatio = 0.0;
	else
		voteRatio = (double)maxVote / totalVote;

	// 计算最佳类别
	if( maxTypeIndex <= 2 ) {
		strDetType.Format("%s", "G");
		if( TrueShotType <= 2 )	nDetOK = 1;
		MainDetType = 0;
	}
	else if( maxTypeIndex <= 4 ) {
		strDetType.Format("%s", "M");
		if( TrueShotType >= 3 && TrueShotType <= 4 )	nDetOK = 1;
		MainDetType = 1;
	}
	else if( maxTypeIndex <= 6 ) {
		strDetType.Format("%s", "C");
		if( TrueShotType >= 5 && TrueShotType <= 6 )	nDetOK = 1;
		MainDetType = 2;
	}
	else if( maxTypeIndex == 7 ) {
		strDetType.Format("%s", "O");
		if( TrueShotType == 7 )	nDetOK = 1;
		MainDetType = 3;
	}
	else {
		strDetType.Format("%s", "##");
		nDetOK = 0;
		MainDetType = -1;
	}

	if(MainDetType >= 0 && MainDetType <= 3) {
		DetMatrx[MainTrueType][MainDetType]++;
	}

	DetMatrx.DeleteMatrix();

	//== 清除数据内存 ==============================================================
	if(pImgBuffer != NULL) {
		delete []pImgBuffer;		
	}
	pImgBuffer = NULL;

	if(iplImage != NULL) {
		iplDeallocateImage(iplImage);
		iplDeallocate(iplImage,IPL_IMAGE_ALL);
	}
	iplImage = NULL;

	pDet.Release();
	
	return MainDetType;
}

/*
*	场景分类(基于帧)
*	IplImage* srcImg	-- 输入的原始彩色图像
*	返回值 int -- 标识场景类型的数,解释如下
*	(0, 1, 2)	-- (left, mid, right) long shot	
*	(3, 4)		-- (active, inactive) medium shot, (基于单帧的不用)
*	(5, 6)		-- (InField, OutField) close-up
*	( 7 )		-- out-of-field view (eg. audience)
*	( -1 )		-- others
*	-------------- 树结构的场景分类
*	特征:
*		1. 场地面积比
*		2. 纹理特征(灰度共生矩阵的对比度)
*		3. 人头区域
*		4. 目标尺度
*	by TONG Xiaofeng	2004.3.12
*/
int CShotClassifier::ViewClassification(IplImage* srcImg)
{
	// =============================================================================
	// L	-- long shot
	// M	-- medium shot
	// CI	-- close-up with field background
	// CO	-- close-up with non-field background
	// OF	-- out-of-field
	// OT	-- others
	// =============================================================================
	// 镜头类别标识
	int view_class = -1;

	// 特征表达 ==================================================
	double	field_ratio = 0.0;
	double	txtu_cont = 0.0;
	BOOL	bHead = FALSE;
	OBJ_FIELD	obj_para;
	obj_para.det_scale = 0;
	obj_para.apox_scale = 0;
	obj_para.area_ratio = 0.0;
	obj_para.ratio_HW = 3;
	tagTexture txtu;
	txtu.ASM = txtu.CONT = txtu.ENTROPY = 0.0;
	// ===========================================================

	// 分割门限 ==================================================
	double	th_field_ratio	= 0.15;	//分割(CI, M, L) vs (CO, OF)
	double	th_txtu_cont	= 55.0; // for (OF) vs (CO)
	int		th_obj_det_scale= 34;	// 检测到的目标的尺度门限
	int		th_obj_apx_scale= 52;	// 近似目标尺度
	double	th_obj_area_ratio = 0.0801;// 目标面积与场地面积的比门限
	// ===========================================================

//	int i,j,r,c;
	int nHeight = srcImg->height;
	int nWidth  = srcImg->width;
	CRect rectROI(0, 0, nWidth, nHeight);

	// step 1 : field segmentation =================================================
	IplImage* fieldImg = cvCreateImage(cvSize(nWidth, nHeight), 8, 1);
	//field_ratio = FieldExtraction(srcImg, fieldImg);
	BOOL bField = FALSE;
	bField = ExistField(srcImg, field_ratio);
	cvReleaseImage(&fieldImg);

	// step 2 : (CI, M, L) vs (OF, CO) with field_ratio
	// === step 2.1 : (CI) vs (L, M) with bHead, if field_ratio is large
	if( field_ratio > th_field_ratio )	{
		bHead = HeadDet(srcImg, NULL, FALSE);
		if( bHead )	{	// has a head, a close-up with field background view
			view_class = 5;	
			
		}
		else {	// estimate object scale in field
			BOOL bObjDet = FALSE;
			bObjDet = ObjExtraction(srcImg, NULL, obj_para, FALSE);
			// 只有正确检测到场地及目标时才可能是长镜头和中镜头
			if( bObjDet ) {
				if( (obj_para.det_scale < th_obj_det_scale && obj_para.apox_scale < th_obj_apx_scale && obj_para.area_ratio > th_obj_area_ratio ) ||\
					(obj_para.det_scale > th_obj_det_scale && obj_para.apox_scale > th_obj_apx_scale ) ||\
					(obj_para.det_scale < th_obj_det_scale && obj_para.apox_scale > th_obj_apx_scale && obj_para.area_ratio > th_obj_area_ratio ) ||\
					(obj_para.det_scale > th_obj_det_scale && obj_para.apox_scale < th_obj_apx_scale && obj_para.area_ratio > th_obj_area_ratio ) ) {
						view_class = 3;	// Medium
				}
				else	
					view_class = 0;	// long
			}
			// 否则作为场外镜头继续分类
			else {
				ImgTexture(srcImg, rectROI, txtu, 2, 2);
				if( txtu.CONT > th_txtu_cont )	// Out-of-field
					view_class = 7;	// Out-of-field
				else
					view_class = 6;	// CloseOF
			}			
		}
	}	// end of (CI, L, M)
	else {	// for( CO, OF)
		ImgTexture(srcImg, rectROI, txtu, 2, 2);
		if( txtu.CONT > th_txtu_cont )	
			view_class = 7;	// Out-of-field
		else
			view_class = 6;	// CloseOF
	}

	return view_class;
}

/*
*	判断画面中是否存在场地,如果存在,返回TRUE并给出场地面积比
*	否则返回FALSE,并将场地面积比赋值为0
*	场地面积比是最初的场地面积比,而不是经过轮廓处理之后的值
*/
BOOL CShotClassifier::ExistField(IplImage* srcImg, double& grass_ratio)
{
	int nHeight = srcImg->height;
	int nWidth  = srcImg->width;

	// 场地分割图像(二值,场地为白色,非场地为黑色)
	IplImage* fieldImg = cvCreateImage(cvSize(nWidth, nHeight), 8, 1);
	cvZero(fieldImg);
	// 场地轮廓图像
	IplImage* fieldContour = cvCreateImage(cvSize(nWidth, nHeight), 8, 1);
	cvZero(fieldContour);

	grass_ratio = FieldExtraction(srcImg, fieldImg);

	//IplImage* dstImg = cvCreateImage(cvSize(nWidth, nHeight), 8, 3);
	//cvZero(dstImg);

	// step 1 : === 场地轮廓提取
	BOOL bFieldCont = FALSE;
	bFieldCont = FindFieldContour(srcImg, fieldImg, fieldContour, NULL, FALSE);

	if(!bFieldCont)	grass_ratio = 0.0;

	cvReleaseImage(&fieldImg);
	cvReleaseImage(&fieldContour);

	return bFieldCont;
}

// 场地分割
double CShotClassifier::FieldExtraction(IplImage* orgImg, IplImage* fieldImg)
{
	// == 分割参考值 HSV color space
	double RefValue[4][3];
	RefValue[0][0]   = 0.2616;       RefValue[0][1] = 0.0328;      RefValue[0][2] = 0.05;     //Hue
	RefValue[1][0]   = 0.5501;       RefValue[1][1] = 0.1219;      RefValue[1][2] = 0.32;//0.3168;   //0.30;     // Saturation
	RefValue[2][0]   = 0.5241;       RefValue[2][1] = 0.0820;      RefValue[2][2] = 0.0;      //Intensity
	// 前3个是中心参考值,第8个是HSV分割距离参考值
	RefValue[3][0]   = 0.1649;       RefValue[3][1] = 0.0762;      RefValue[3][2] = 0.34;//   // 0.30;  // HSV
	// == 为了方和计算速度,暂时不统计当前图像的颜色均值,只采用先验值

	//== step 1 : rgb --> hsv color space ============================================
/*	IplImage *hsvImg = NULL;	// HSV 色彩图像,由rgb色彩转换
	hsvImg = cvCreateImage(cvSize(iplImg->width, iplImg->height), IPL_DEPTH_8U, 3);
	//iplRGB2HSV(iplImg, hsvImg);
	cvCvtColor(iplImg, hsvImg, CV_RGB2HSV);
*/
	IplImage *hsvImg = NULL;	// HSV 色彩图像,由rgb色彩转换
	hsvImg = iplCloneImage(orgImg);
	iplRGB2HSV(orgImg, hsvImg);

	cvSetZero(fieldImg);

	//== step 2 : compute hsv color distance and classify for each point =============
	int nHeight = hsvImg->height;
	int nWidth  = hsvImg->width;
	int widthByte = hsvImg->widthStep;

	double grass_ratio = 0.0;		// 草地象素比例
	DWORD dwGrassPixel = 0;			// 草地象素个数
	
	int th_low = 30;
	int th_high = 220;
	int meanGry = 0;
	BYTE red, green, blue;
	
	int row, col;
	double dist_int, dist_chr, dist_hsv, theta;

	double df_pixel[3] = {0.0, 0.0, 0.0};
	for(row = 0; row < nHeight; row++) {
		for(col = 0; col < nWidth; col++) {
			// step 1: 首先用亮度值滤除一部分
			meanGry = 0;
			red		= (BYTE)(*(orgImg->imageData + row*orgImg->widthStep + 3*col));
			green	= (BYTE)(*(orgImg->imageData + row*orgImg->widthStep + 3*col + 1));
			blue	= (BYTE)(*(orgImg->imageData + row*orgImg->widthStep + 3*col + 2));
		
			meanGry = (red + green + blue) / 3;

			if( meanGry >= th_low && meanGry <= th_high ) {
				//iplGetPixel(hsvImg, col, row, (BYTE *)(un_pixel));
				df_pixel[0] = ( (BYTE)(*(hsvImg->imageData + row*hsvImg->widthStep + 3*col)) ) / 255.0;
				df_pixel[1] = ( (BYTE)(*(hsvImg->imageData + row*hsvImg->widthStep + 3*col + 1)) ) / 255.0;
				df_pixel[2] = ( (BYTE)(*(hsvImg->imageData + row*hsvImg->widthStep + 3*col + 2)) ) / 255.0;
			
				dist_int = 0;	//df_pixel[2] - RefValue[2][0]; // 为0,不考虑
				/*
				if( fabs(RefValue[0][0] - df_pixel[0]) < 0.5 )   // hue < [0.0, 1.0], 0.5 -- 180 degree
					theta = fabs(RefValue[0][0] - df_pixel[0]);
				else 
					theta = 1 - fabs(RefValue[0][0] - df_pixel[0]);
				*/
				theta = RefValue[0][0] - df_pixel[0];
				theta = 2*3.1415926*theta; // 转化为弧度值
				dist_chr = sqrt( df_pixel[1]*df_pixel[1] + RefValue[1][0]*RefValue[1][0] \
								- 2*df_pixel[1]*RefValue[1][0]*cos(theta) );

				dist_hsv = sqrt( dist_int*dist_int + dist_chr*dist_chr );
    
				if( dist_hsv < RefValue[1][2] /* [3][2] */ ) {	//  属于场地象素
					dwGrassPixel ++;			
					fieldImg->imageData[row*fieldImg->widthStep + col] = (char)255;			
				}
			}
		}
	}

	if(hsvImg != NULL) {
		iplDeallocate( hsvImg, IPL_IMAGE_HEADER | IPL_IMAGE_DATA );
		hsvImg = NULL;
	}
	/*
	if(hsvImg != NULL) {
		cvReleaseImage(&hsvImg);
		hsvImg = NULL;
	}
	*/

	grass_ratio = (double)(dwGrassPixel)/(nHeight*nWidth);
	return grass_ratio;
}
/*
*	寻找场地的凸轮廓
*	输入原始彩色图像,输出标定场地轮廓的彩色图像
*	当场地面积比很小时,返回false,不做事情
*	IplImage* srcImg	--- 输入的原始彩色图像 
*	IplImage* dstImg	--- 输出的标定场地区和目标的图像
*	IplImage* fieldImg	--- 场地提取图像(二值)
*	IplImage* fieldContImg---	场地轮廓图像(场地轮廓内全部为白色1,外部为黑色0)
*	BOOL bTest	= TRUE  用于试用版, 可以看到中间结果和最后结果
*	BOOL bTest	= FALSE 用于实用版, 只有最后结果
*/
BOOL CShotClassifier::FindFieldContour(IplImage* srcImg, IplImage* fieldImg, IplImage* fieldContImg, IplImage* dstImg, BOOL bTest)
{
	int i,j,r,c;
	int nHeight = srcImg->height;
	int nWidth = srcImg->width;

	// step 1 ---- 场地提取-----------------------------------------------------------
	//IplImage* fieldImg = cvCreateImage( cvGetSize(srcImg), IPL_DEPTH_8U, 1 );
	double grass_ratio = 0.0;
	grass_ratio = FieldExtraction(srcImg, fieldImg);	// 场地用白色表示
	IplImage* fedTmpImg = cvCreateImage( cvGetSize(srcImg), IPL_DEPTH_8U, 1 );
	cvCopyImage(fieldImg, fedTmpImg);
	
	if(grass_ratio < 0.1) {	// 场地面积太小,返回不做
		cvReleaseImage(&fedTmpImg);
		return FALSE;
	}

	// 对分割图作一个形态学滤波比较好
	IplImage* morphImg = cvCreateImage( cvGetSize(srcImg), IPL_DEPTH_8U, 1 );
	cvZero(morphImg);
	// "open" 运算去掉 0 区中的 1, "close"运算去掉 1 区中的 0
	int nItera = 1;		
	iplOpen(fedTmpImg, morphImg,  nItera);		
	iplClose(morphImg, fedTmpImg, nItera);
	
	// step 2 ---- 区域整合,去掉截断场地的线条(小目标)--------------------------------
	IplImage* tmpImg = cvCreateImage( cvGetSize(srcImg), IPL_DEPTH_8U, 1 );
	cvZero(tmpImg);

	int nBlockSize = 8;	// 块太小,有些区域搞不定(不能连接)
	int nBlockNum_H = nWidth/nBlockSize;
	int nBlockNum_V = nHeight/nBlockSize;
	double th_fed_ratio = 0.5;
	for(i=0; i < nBlockNum_V; i++) {
		for(j=0; j < nBlockNum_H; j++) {
			int accuPxl = 0;
			for(r=i*nBlockSize; r<i*nBlockSize+nBlockSize; r++) {
				for(c=j*nBlockSize; c<j*nBlockSize+nBlockSize; c++) {
					if( *(fedTmpImg->imageData + r*fedTmpImg->widthStep + c) != 0 )
						accuPxl++;
				}
			}
			if( (double)accuPxl / (nBlockSize*nBlockSize) > th_fed_ratio ) {
				for(r=i*nBlockSize; r<i*nBlockSize+nBlockSize; r++) {
					for(c=j*nBlockSize; c<j*nBlockSize+nBlockSize; c++) {
						*(tmpImg->imageData + r*tmpImg->widthStep + c) = (BYTE)255;						
					}
				}
			}
		}
	}
	//cvNamedWindow( "fedTmpImg", 1 );
	//cvShowImage( "fedTmpImg", fedTmpImg );
	//cvNamedWindow( "tmpImg1", 1 );
	//cvShowImage( "tmpImg1", tmpImg1 );
	
	// 填充非场地中的目标区域(使得非场地区中不含有场地区) ============================
	// inverse image
	for(r=0; r<nHeight; r++) {
		for(c=0; c<nWidth; c++) {
			*(tmpImg->imageData+r*tmpImg->widthStep+c) = 
				(BYTE)(255 - (BYTE)(*(tmpImg->imageData+r*tmpImg->widthStep+c)));
		}
	}
	// 保存tmpImg,用于下次的轮廓跟踪
	cvCopyImage(tmpImg, morphImg);

	// step 3 : ----- 轮廓分析 -------------------------------------------------------
	CvMemStorage* storage = 0;
	storage = cvCreateMemStorage(0);
	CvSeq* contour = 0;	
	CvSeq* result = 0;

	int contNum = cvFindContours( tmpImg, storage, &contour, sizeof(CvContour), 
		CV_RETR_EXTERNAL, CV_CHAIN_APPROX_SIMPLE );

	// 找面积最大的轮廓区
	BOOL bFieldCont = FALSE;
	int max_index = 0, contIndex = 0;
	double max_area = 0.0, area = 0.0;
	while( contour )     {
		bFieldCont = TRUE;

		result = cvApproxPoly( contour, sizeof(CvContour), storage,
					CV_POLY_APPROX_DP, cvContourPerimeter(contour)*0.02, 0 );
		area = fabs(cvContourArea(result,CV_WHOLE_SEQ));

		if (max_area < area) {
			max_area = area;
			max_index = contIndex;
		}
	
		contour = contour->h_next;
		contIndex++;
	}
	cvClearMemStorage( storage );
	cvReleaseMemStorage(&storage);	storage = NULL;
	
	storage = cvCreateMemStorage(0);
	if(bFieldCont) {			
		// 恢复tmpImg,再次进行轮廓跟踪
		cvCopyImage(morphImg, tmpImg);
		cvFindContours( tmpImg, storage, &contour, sizeof(CvContour), 
			CV_RETR_EXTERNAL, CV_CHAIN_APPROX_SIMPLE );

		contIndex = 0;
		while( contour )     {	
			if(contIndex == max_index) {					
				
				CvPoint* ptSeq = new CvPoint[contour->total];
				for(int ii = 0; ii < contour->total; ii++ ) {
					CvPoint *pt = (CvPoint *)cvGetSeqElem(contour, ii, 0);
					//cvSeqPush( ptSeq, &pt );
					ptSeq[ii] = *pt;
				}
				// 直接将tmpImg1中的黑色区域填充为 0
				cvFillPoly(fedTmpImg, &ptSeq, &(contour->total), 1, CV_RGB(0,0,0));
			
				delete []ptSeq;
				break;
			}
			else {
				contour = contour->h_next;
				contIndex++;
			}
		}
	}
	cvClearMemStorage( storage );
	cvReleaseMemStorage(&storage);	storage = NULL;

//	cvNamedWindow( "fedTmpImg", 1 );
//	cvShowImage( "fedTmpImg", fedTmpImg );

//	cvSaveImage("E:\\Documents\\My_papers\\ICSP04\\fieldLink.bmp", fedTmpImg);
	
	// 对正常的场地图像做轮廓跟踪
	BOOL bField = FALSE;
	bField = GetFieldConvexContour(fedTmpImg, grass_ratio, fieldContImg, dstImg, bTest);

	// release image	
	cvReleaseImage(&morphImg);
	cvReleaseImage(&tmpImg);
	cvReleaseImage(&fedTmpImg);	

	return bField;
}

/* 
*	计算场地凸形区域的轮廓
*	输入是已经填充过非场地的图像,输出是标定场地轮廓的图像
*	IplImage* segImg	-- 已经填充非场地区的二值图像
*	double dfFieldRatio	-- 场地面积比
*	IplImage* fieldContImg	-- 返回的场地轮廓图像
*	IplImage* dstImg	-- 原始图像的拷贝,用于画出场地轮廓
*	BOOL	  bTest		-- 是否需要显示原图像 
*
*	返回BOOL,如果检测到场地轮廓则返回TRUE
*	可能的误检导致部分图像中存在大的场地面积,但不一定是真实的场地
*/
BOOL CShotClassifier::GetFieldConvexContour(IplImage* segImg, double dfFieldRatio, IplImage* fieldContImg, IplImage* dstImg, BOOL bTest)
{
	// 主要问题是判断场地是否被隔断,然后连接
	int nHeight = segImg->height;
	int nWidth	= segImg->width;

	// 场地像素个数
	int nFieldArea = (int)(dfFieldRatio*nHeight*nWidth);

	// 对segImg进行迭代操作,填充被黑色包围的白色块
	IplImage* fillImg = cvCreateImage( cvSize(nWidth, nHeight), 8, 1 ); 
	cvCopyImage(segImg, fillImg);

	int i,j,ii,jj;
	int nBlockSize = 8;
	int rowBlock = nHeight/nBlockSize;
	int colBlock = nWidth /nBlockSize;
	CMatrix<int> BlockMat(rowBlock, colBlock);
	BOOL bFill = TRUE;	// 是否填充的标志,当没有填充时,为FALSE,循环退出
	for(i=0; i<rowBlock; i++) {
		for(j=0; j<colBlock; j++) {
			int onePixl = 0;
			for(ii=i*nBlockSize; ii < i*nBlockSize+nBlockSize; ii++) {
				for(jj=j*nBlockSize; jj < j*nBlockSize+nBlockSize; jj++) {
					if( *(fillImg->imageData + ii*fillImg->widthStep+jj) != 0 ) {
						onePixl++;
					}
				}
			}
			if( onePixl >= nBlockSize*nBlockSize/2 ) {
				BlockMat[i][j] = 1;					
			}
		}
	}
	int allNeibor = 0;
	int zeroNeibor = 0, oneNeibor = 0;
	while(bFill) {
		bFill = FALSE;	
		// 填充被黑色包围的白色区,四邻域	
		for(i=0; i<rowBlock; i++) {
			for(j=0; j<colBlock; j++) {
				if(BlockMat[i][j] == 1) {
					allNeibor = 0;
					zeroNeibor = 0;
					// up
					if( (i-1) >= 0 ) {
						allNeibor++;
						if( BlockMat[i-1][j] == 0 )
							zeroNeibor++;
					}
					// down
					if( (i+1) < rowBlock ) {
						allNeibor++;
						if( BlockMat[i+1][j] == 0 )
							zeroNeibor++;
					}
					// left
					if( (j-1) >= 0 ) {
						allNeibor++;
						if( BlockMat[i][j-1] == 0 )
							zeroNeibor++;
					}
					// right
					if( (j+1) < colBlock ) {
						allNeibor++;
						if( BlockMat[i][j+1] == 0 )
							zeroNeibor++;
					}

					if( zeroNeibor > allNeibor/2 ) {
						BlockMat[i][j] = 0;
						bFill = TRUE;
					}
				}
			}
		} // end of fill while surrounded by black

		// 填充被白色包围的黑色区,四邻域		
		for(i=0; i<rowBlock; i++) {
			for(j=0; j<colBlock; j++) {
				if(BlockMat[i][j] == 0) {
					allNeibor = 0;
					oneNeibor = 0;
					// up
					if( (i-1) >= 0 ) {
						allNeibor++;
						if( BlockMat[i-1][j] != 0 )
							oneNeibor++;
					}
					// down
					if( (i+1) < rowBlock ) {
						allNeibor++;
						if( BlockMat[i+1][j] != 0 )
							oneNeibor++;
					}
					// left
					if( (j-1) >= 0 ) {
						allNeibor++;
						if( BlockMat[i][j-1] != 0 )
							oneNeibor++;
					}
					// right
					if( (j+1) < colBlock ) {
						allNeibor++;
						if( BlockMat[i][j+1] != 0 )
							oneNeibor++;
					}

					if( oneNeibor > allNeibor/2 ) {
						BlockMat[i][j] = 1;
						bFill = TRUE;
					}
				}
			}
		}// end of fill black surrounded by white

	}

	cvZero(fillImg);
	//改变图像
	for(i=0; i<rowBlock; i++) {
		for(j=0; j<colBlock; j++) {
			if( BlockMat[i][j] == 1 ) {
				for(ii = i*nBlockSize; ii < i*nBlockSize+nBlockSize; ii++) {
					for(jj = j*nBlockSize; jj < j*nBlockSize+nBlockSize; jj++) {
						*(fillImg->imageData + ii*fillImg->widthStep+jj) = (BYTE)255;
					}
				}
			}
		}
	}	
	BlockMat.DeleteMatrix();

//	cvSaveImage("C:\\MD_301.bmp", fillImg);

	// 估计场地线的倾斜角度,拟合出边缘线
	double delta_x = 0.0, delta_y = 0.0;
	int pt_x = 0, pt_y = 0;
	int line_flag = 1;	// hough
	FieldEdgeFit(fillImg, delta_x, delta_y, pt_x, pt_y, line_flag);
	double tan_theta;
	if( fabs(delta_x) < 0.0001 ) {		
		tan_theta = 100000.0;		
	}
	if( fabs(delta_y) < 0.0001 ) {		
		tan_theta = 0.0;
	}
	else {	
		tan_theta = delta_y / delta_x;
	}
	//	得到某个点的坐标的方法(假设已知x坐标,求y坐标)=============
	//	y = (int)(pt_y + tan_theta*(x - pt_x));
	//	==========================================================
//	if(bTest) {
//		cvNamedWindow("fillImg", 1);
//		cvShowImage("fillImg", fillImg);
//	}

//	cvSaveImage("C:\\fedTmp.bmp", segImg);
//	cvSaveImage("C:\\fillImg.bmp", fillImg);

	// 轮廓分析后图像数据已经改变,所以要求保持原数据,同时拷贝合理数据区域
	IplImage* tmpImg = cvCreateImage( cvSize(nWidth, nHeight), 8, 1 ); 
	cvCopyImage(fillImg, tmpImg);

	// step 1: -- 计算最大轮廓面积,如果小于估算面积一定程度,则认为有截断
	CvMemStorage* storage = 0;
	storage = cvCreateMemStorage(0);
	CvSeq* contour = 0;	
	CvSeq* copy_cont = 0;
	CvSeq* result = 0;
	CvMoments* moments = new CvMoments;	// 区域矩特征
	int cent_x=0, cent_y=0;
	int rect_w=0, rect_h=0;
	CvRect rect;
	//CvPoint* pt = 0;
 
	// select the maximum ROI in the image  
	cvSetImageROI( tmpImg, cvRect( 0, 0, nWidth, nHeight ));

	int contNum = cvFindContours( tmpImg, storage, &contour, sizeof(CvContour), 
		CV_RETR_EXTERNAL, CV_CHAIN_APPROX_SIMPLE );

	// 记录每个矩形区的信息,然后连接
	int *arrRectCent = new int[6*contNum];	// (cent_x, cent_y, width, height, area, bOk),bOk 表示是否合格的区域(在分界线下)
	memset(arrRectCent, 0, 6*contNum*sizeof(int));
	
	// 找面积最大的轮廓区
	BOOL bFieldCont = FALSE;
	int max_index = 0, contIndex = 0;
	double max_area = 0.0, area = 0.0;
	while( contour )     {
		bFieldCont = TRUE;

		result = cvApproxPoly( contour, sizeof(CvContour), storage,
					CV_POLY_APPROX_DP, cvContourPerimeter(contour)*0.02, 0 );
		rect  = cvContourBoundingRect( result, 0);
		area = fabs(cvContourArea(result,CV_WHOLE_SEQ));

		if (max_area < area) {
			max_area = area;
			max_index = contIndex;
			/*// 区域中心
			cvContourMoments(result, moments);
			cent_x = (int)(moments->m10/moments->m00);
			cent_y = (int)(moments->m01/moments->m00);
			rect_w = rect.width;
			rect_h = rect.height;
			*/
		}
		
		// 区域中心
		cvContourMoments(result, moments);
		cent_x = (int)(moments->m10/moments->m00);
		cent_y = (int)(moments->m01/moments->m00);

		arrRectCent[6*contIndex]	= cent_x;
		arrRectCent[6*contIndex+1]	= cent_y;
		arrRectCent[6*contIndex+2]	= rect.width/2;
		arrRectCent[6*contIndex+3]	= rect.height/2;
		arrRectCent[6*contIndex+4]	= (int)area;
		// 判断中心点是否在分界线以下
		if( cent_y >= (int)(pt_y + tan_theta*(cent_x - pt_x)) ) {
			arrRectCent[6*contIndex+5]	= 1;
		}
		else {
			arrRectCent[6*contIndex+5]	= 0;
		}
		
		contour = contour->h_next;
		contIndex++;
	}
	delete moments;	moments = NULL;
	cvClearMemStorage( storage );	
	cvReleaseMemStorage(&storage);storage = NULL;
	
	// 判断最大面积与估算面积的比较
	if ( contNum > 1 /*max_area / nFieldArea < th_ratio*/) {	// 被截断,连接截断
		// 首先估计需要连接的场地的左右端点,上下端点
		// 简单的估计
		/*
		int left_x = 0;
		int right_x = nWidth-1;
		//int top_y = (int)( (1.0 - dfFieldRatio)*nHeight );
		int top_y = max(cent_y - rect_h/2, 0);
		int bottom_y = min(cent_y + rect_h/2, nHeight-1);

		CvPoint leftPt, rightPt, topPt, bottomPt;
		leftPt.x = left_x;	leftPt.y = cent_y;
		rightPt.x = right_x; rightPt.y = cent_y;
		topPt.x = cent_x;	topPt.y = top_y;
		bottomPt.x =cent_x;	bottomPt.y = bottom_y;

		cvLine(tmpImg, leftPt, rightPt, 3, 8);
		cvLine(tmpImg, topPt, bottomPt, 3, 8);
		*/
		// 连接所有满足条件的区域的中心
		CvPoint centPt1, centPt2;
		centPt1.x = arrRectCent[6*max_index];
		centPt1.y = arrRectCent[6*max_index+1];
		for(i=0; i<contNum; i++) {
			// 在分界线以下或者离主区域很近(高度相仿,中心高度相仿),则合格
			if( (arrRectCent[6*i+5] == 1) ||\
				( (arrRectCent[6*i+3] > arrRectCent[6*max_index+3]/2) && (abs(arrRectCent[6*i+1] - arrRectCent[6*max_index+1]) < arrRectCent[6*max_index+1]/2)) )
			{	// 合格
				centPt2.x = arrRectCent[6*i];
				centPt2.y = arrRectCent[6*i+1];
				cvLine(fillImg, centPt1, centPt2, 3, 8);
			}
		}

		cvCopyImage(fillImg, tmpImg);		
		// 再对连接后的图像进行轮廓提取,并计算场地轮廓
		// select the maximum ROI in the image  
		cvSetImageROI( tmpImg, cvRect( 0, 0, nWidth, nHeight ));

		storage = cvCreateMemStorage(0);
		contNum = cvFindContours( tmpImg, storage, &contour, sizeof(CvContour), 
			CV_RETR_EXTERNAL, CV_CHAIN_APPROX_SIMPLE );

		// 找面积最大的轮廓区
		bFieldCont = FALSE;
		max_index = 0, contIndex = 0;
		max_area = 0.0, area = 0.0;
		while( contour )     {
			bFieldCont = TRUE;

			result = cvApproxPoly( contour, sizeof(CvContour), storage,
						CV_POLY_APPROX_DP, cvContourPerimeter(contour)*0.02, 0 );
			
			area = fabs(cvContourArea(result,CV_WHOLE_SEQ));

			if (max_area < area) {
				max_area = area;
				max_index = contIndex;
			}
		
			contour = contour->h_next;
			contIndex++;
		}
		cvClearMemStorage( storage );
		cvReleaseMemStorage(&storage);
	}
	// 填充最大面积的区域,得到场地凸轮廓
	if(bFieldCont) {
		cvCopyImage(fillImg, tmpImg);
		storage = cvCreateMemStorage(0);
		contNum = cvFindContours( tmpImg, storage, &contour, sizeof(CvContour), 
			CV_RETR_EXTERNAL, CV_CHAIN_APPROX_SIMPLE );

		contIndex = 0;
		while( contour )     {	
			if(contIndex == max_index) {					
				CvSeq* hull;
				hull = cvContourConvexHull( contour, CV_COUNTER_CLOCKWISE, 0 );				
				int hullcount = hull->total;

				// 在原图中标定场地轮廓
				if( bTest ) {
					CvPoint pt0 = **CV_GET_SEQ_ELEM( CvPoint*, hull, hullcount - 1 );
					for( i = 0; i < hullcount; i++ )
					{		              
						CvPoint pt = **CV_GET_SEQ_ELEM( CvPoint*, hull, i );
		
						cvLine( dstImg, pt0, pt, CV_RGB( 255, 0, 0 ), 3, 0 );
						pt0 = pt;

						//draw_contour[i] = pt;
						//cvSeqPush( draw_cont, &pt );
					}
				}
								
				CvPoint* ptSeq = new CvPoint[hullcount];
				for(int ii = 0; ii < hullcount; ii++ ) {
					//CvPoint *pt2 = (CvPoint *)cvGetSeqElem(hull, ii, 0);
					CvPoint pt = **CV_GET_SEQ_ELEM( CvPoint*, hull, ii );
					//cvSeqPush( ptSeq, &pt );
					ptSeq[ii] = pt;
				}
				cvFillPoly(fieldContImg, &ptSeq, &(hullcount), 1, CV_RGB(255,255,255));					
				delete []ptSeq;
				//if(hull) free(hull);
				break;
			}
			else {
				contour = contour->h_next;
				contIndex++;
			}
		}
		cvClearMemStorage( storage );
		cvReleaseMemStorage(&storage);
	}

	delete []arrRectCent;

	cvReleaseImage(&tmpImg);
	cvReleaseImage(&fillImg);

	// 如果场地面积比小于1/3,并且中心位于场地下端,则返回FALSE;
	// added by TONG, 2004.4.10
	int FieldCent_y = 0;
	int pxl = 0;
	DWORD m_00 = 0, m_01 = 0;
	for(i=0; i<nHeight; i++) {
		for(j=0; j < nWidth; j++) {
			pxl = (int)((BYTE)(*(fieldContImg->imageData + i*fieldContImg->widthStep + j))/255);
			m_00 = m_00 + pxl;
			m_01 = m_01 + i*pxl;
		}
	}
	if( m_00 <= 0 )	FieldCent_y = 0;
	else 
		FieldCent_y = m_01/m_00;

	if( dfFieldRatio < 1.0/3 && FieldCent_y < nHeight/2 ) {
		cvZero(fieldContImg);
		bFieldCont = FALSE;
	}

	return bFieldCont;
}
/*
*	提取场地中的目标,并估计其尺度(可以是平均尺度,也可以是其它
*	说明:
*	1. 本函数需要在场地提取和场地轮廓提取之后调用
*	2. IplImage* srcImg --- 原始图像,彩色
*	3. IplImage* dstImg		--- 显示结果的图像
*	4. OBJ_FIELD& obj_para	--- 包含场地中目标信息的结构
*	5. BOOL	bTest	--- 测试用,TRUE时显示结果,FALSE只返回尺度信息
*/
BOOL CShotClassifier::ObjExtraction(IplImage* srcImg, IplImage* dstImg, OBJ_FIELD& obj_para, BOOL bTest)
{	
	int i,j,ii,jj;
	int nHeight = srcImg->height;
	int nWidth  = srcImg->width;

	// 场地分割图像(二值,场地为白色,非场地为黑色)
	IplImage* fieldImg = cvCreateImage(cvSize(nWidth, nHeight), 8, 1);
	cvZero(fieldImg);
	// 场地轮廓图像
	IplImage* fieldContour = cvCreateImage(cvSize(nWidth, nHeight), 8, 1);
	cvZero(fieldContour);

	// step 1 : === 场地轮廓提取
	BOOL bFieldCont = FALSE;
	bFieldCont = FindFieldContour(srcImg, fieldImg, fieldContour, dstImg, bTest);
//	if(bTest) {
//		cvNamedWindow( "field", 1 );
//		cvShowImage( "field", fieldImg );
//		cvNamedWindow( "fieldCont", 1 );
//		cvShowImage( "fieldCont", fieldContour );
//	}
	
	// 只有当正确提取出场地时才继续进行,否则退出
	if( bFieldCont ) {
		// 估计目标区域高度与场地面积高度之比,先计算场地的平均高度
		CMatrix<int>  fedContProj_H(1, nWidth);	// 场地区域水平投影
		for(j = 0; j < nWidth; j++) {
			for(i = 0; i < nHeight; i++) {
				if( *(fieldContour->imageData + i*fieldContour->widthStep + j) != 0 ) {
					fedContProj_H[0][j]++;
				}
			}
		}
		double valid_proj_th = 0.1;
		DWORD	dwTotalFedPxl = 0, dwValidCol = 1;
		for(i = 0; i < nWidth; i++) {
			if( double(fedContProj_H[0][i]) / nHeight >= valid_proj_th ) {
				dwValidCol++;
				dwTotalFedPxl += (fedContProj_H[0][i]);
			}
		}
		int nFieldHeight = (int) (dwTotalFedPxl / dwValidCol);
		if( nFieldHeight <= 0 )	nFieldHeight = 1;

		// === 对fieldContour进行矩形结构的腐蚀,去掉边缘部分的影响 =================
		int kerWidth = 15;
		int kernel_rect[15*15];
		for(i=0; i<kerWidth*kerWidth; i++)	kernel_rect[i] = 255;
		IplConvKernel *kernel = iplCreateConvKernel( kerWidth, kerWidth, kerWidth/2, kerWidth/2, kernel_rect, 0 );
		IplImage* erodeImg = cvCreateImage(cvGetSize(srcImg), 8, 1);
		cvZero(erodeImg);
		cvErode(fieldContour, erodeImg, kernel, 1);
		iplDeleteConvKernel( kernel);

		cvCopyImage(erodeImg, fieldContour);

		cvReleaseImage(&erodeImg);
		// ============================================================================


		// 草地区域的面积,用于评估目标面积于草地面积的比例
		DWORD dwFieldArea = 0;
		for(i=0; i<nHeight; i++) {
			for(j=0; j<nWidth; j++) {
				if( *(fieldImg->imageData + i*fieldImg->widthStep + j) != 0 ) {
					dwFieldArea += 1;
				}
			}
		}

		// step 2 : === 目标提取并估计其尺度
		IplImage* ObjContImg = cvCreateImage(cvGetSize(srcImg), 8, 1);
		cvZero(ObjContImg);

		// step 2.1: 得到场地中的目标区域(用白色(255))表示 ==================================
		// 当一个像素在fieldContour为 1,而在fieldImg为 0 时,
		// 即在场地中,但不是场地的目标,此时设为1
		DWORD dwObjArea = 0;	// 场地中目标的面积
		for(i=0; i<nHeight; i++) {
			for(j=0; j<nWidth; j++) {
				if( *(fieldContour->imageData + i*fieldContour->widthStep + j) != 0 &&\
					*(fieldImg->imageData + i*fieldImg->widthStep + j) == 0 ) {
					*(ObjContImg->imageData + i*ObjContImg->widthStep + j) = (BYTE)255;
					dwObjArea++;
				}
			}
		}
		// 目标区域与场地区域面积之比
		obj_para.area_ratio = (double)(dwObjArea) / (dwFieldArea);

		// step 2.2: 对场地中的目标的形态学滤波 =============================================
		IplImage* morphImg = cvCreateImage(cvGetSize(srcImg), 8, 1);
		int nItera = 1;
		// "open" 运算去掉 0 区中的 1, "close"运算去掉 1 区中的 0
		// open --> first erode,then dilate; close --> first dilate,then erode
	//	iplOpen(ObjContImg ,morphImg,  nItera);		
		iplClose(ObjContImg, morphImg, nItera);
		iplOpen(morphImg, ObjContImg ,  nItera);	
		cvCopyImage(ObjContImg, morphImg);

	//	cvSaveImage("C:\\GL_299.bmp", ObjContImg);
	//	if(bTest) {
	//		cvNamedWindow( "ObjImg", 1 );
	//		cvShowImage( "ObjImg", ObjContImg );
	//	}

	//	cvSaveImage("E:\\Documents\\My_papers\\ICSP04\\ObjFed.bmp", fieldImg);
	//	cvSaveImage("E:\\Documents\\My_papers\\ICSP04\\ObjSrc.bmp", ObjContImg);

		// step 3: 场地目标的统计(包括利用形状约束滤除不符合条件的区域)
		CvRect	rect;	// 区域包络矩形
		double	sh_solidity, sh_eccent;	//sh_compact, 
		double area		= 0.0;	// 区域面积
		double perim	= 0.0;	// 周长
		double meanGray = 0.0;

		int		th_size_min = 7;	// 最小矩形尺寸 (不小于此)
		int		th_size_max = nWidth-1;	// 最大矩形尺寸 (宽度不大于此)
		double	th_solidity = 0.25;	// solidity = area / (Dx*Dy), 不小于此
		double  th_eccent_max = 3.0;	// eccentrity = Dy/Dx, 不大于此
		double  th_eccent_min = 0.75;	// eccentrity = Dy/Dx, 不小于此
		int		th_mean_gry = 90;

		CvMemStorage* storage = 0;
		storage = cvCreateMemStorage(0);
		CvSeq* contours = 0;	
		CvSeq* result;
		CvPoint* pt = 0;  

		CArray<CRect, CRect&> arrObjRect;
		arrObjRect.RemoveAll();
		CRect aRect;

		// select the maximum ROI in the image  
		cvSetImageROI( morphImg, cvRect( 0, 0, nWidth, nHeight ));
      
		// find contours and store them all as a list  
		int ContourNum=cvFindContours( morphImg, storage, &contours, sizeof(CvContour),  CV_RETR_LIST, CV_CHAIN_APPROX_NONE );
            
		while( contours )     {
			result = cvApproxPoly( contours, sizeof(CvContour), storage,
						CV_POLY_APPROX_DP, cvContourPerimeter(contours)*0.02, 0 );
			area = fabs(cvContourArea(result,CV_WHOLE_SEQ));
			rect  = cvContourBoundingRect( result, 0);
			perim = cvContourPerimeter(contours);

			// 逐层剔除掉不符条件的区域 ==================
			// cont.1 -- size cond
			if( min(rect.width, rect.height) < th_size_min || rect.width >= th_size_max) {
				contours = contours->h_next; 
				continue;
			}

			/*	
			// cont.2 -- compact cond 由于肤色提取不佳,暂时不用
			sh_compact = 4*3.1415926*area / (perim*perim);
			if( sh_compact < th_compact ) {
				contours = contours->h_next; 
				continue;
			}
			*/

			// cont.3 -- solidity cond
			sh_solidity = (double)area/(rect.width*rect.height);
			if( sh_solidity < th_solidity ) {
				contours = contours->h_next; 
				continue;
			}

			// cont.4 -- eccent cond
			sh_eccent = (double)rect.height/rect.width;
			if( sh_eccent < th_eccent_min || sh_eccent > th_eccent_max ) {
				contours = contours->h_next; 
				continue;
			}

			// cont.5 -- 内部为黑色,不符条件
			//cvSetImageROI( fieldImg, rect );
			//meanGray = cvMean(fieldImg, 0);	// 这样计算颜色均值不可靠,原因不明
			meanGray = 0.0;
			for(ii = rect.y+1; ii < rect.y+rect.height-1; ii++) {
				for(jj = rect.x+1; jj < rect.x+rect.width-1; jj++) {
					//iplGetPixel(fieldImg, jj, ii, tmpPixel);
					// data bytes aligned with bytes that can divided by 4, actual bytes per line is "widthStep"
					meanGray += (BYTE)(ObjContImg->imageData[ii*ObjContImg->widthStep + jj]);
					//tmpGray  += (BYTE)(fieldImg->imageData[ii*nWidth + jj]);
				}
			}
			meanGray /= ((rect.height-2)*(rect.width-2));
			if(meanGray < th_mean_gry)	{// 区域内主要为0,不处理
				contours = contours->h_next; 
				continue;
			}

			// (1)== 计算圆形性参数
			//cvContourMoments(result, moments);
			//cent_x = moments->m10/moments->m00;
			//cent_y = moments->m01/moments->m00;

			// 记录人头区域的信息
			aRect.top	 = (int)rect.y;
			aRect.left   = (int)rect.x;
			aRect.bottom = rect.y + rect.height;
			aRect.right  = rect.x + rect.width;
			arrObjRect.Add(aRect);
			  
			contours = contours->h_next;    
		}

	//	delete moments;	moments = NULL;
		cvClearMemStorage( storage );	
		cvReleaseMemStorage (&storage);storage = NULL;

		// 计算目标区域的尺度(平均一下)
		// 可以先对获得的区域滤波,滤除一部分区域,原则 =================================
		obj_para.det_scale = 0;	
		obj_para.apox_scale = 0;
		if(arrObjRect.GetSize() >= 1) {
			for(ii=0; ii < arrObjRect.GetSize(); ii++) {
				aRect = arrObjRect.GetAt(ii);
				obj_para.det_scale += aRect.Height();
				//obj_area_ratio += (aRect.Height() * aRect.Width());
				
				//	画矩形
				if(bTest) {
					cvRectangle( dstImg, cvPoint(aRect.left, aRect.top), cvPoint(aRect.right, aRect.bottom), CV_RGB(255,0,0), 3 );
				}
			}
			obj_para.det_scale /= (arrObjRect.GetSize());
			//obj_area_ratio /=  (arrObjRect.GetSize());
			obj_para.apox_scale = (int)sqrt(obj_para.ratio_HW*dwObjArea/arrObjRect.GetSize());
			obj_para.height_ratio = (double)(obj_para.det_scale) / nFieldHeight;
		}

		arrObjRect.RemoveAll();

		cvReleaseImage(&morphImg);
		cvReleaseImage(&ObjContImg);
	}

	cvReleaseImage(&fieldImg);
	cvReleaseImage(&fieldContour);

	if(bFieldCont && obj_para.det_scale > 0)	return TRUE;
	else 
		return FALSE;
}

/*
*	人头检测
*	step 1: 肤色检测
*	step 2: 连接元分析 (形状约束)
*	step 3: 经验判断
*	IplImage* srcImg	-- 原始彩色图像
*	IplImage* dstImg	-- 标定人头区的图像(如果存在)
*	BOOL bTest			-- 测试标志,TRUE时显示中间结果,否则不显示
*	BOOL				-- 返回值(是否有人头区,有为TRUE,否则为FALSE
*/
BOOL CShotClassifier::HeadDet(IplImage* srcImg, IplImage* dstImg, BOOL bTest)
{
	BOOL bHead = FALSE;

	int row, col, ii, jj;	
	int nHeight = srcImg->height;
	int nWidth  = srcImg->width;

	IplImage* skinImg = cvCreateImage(cvSize(nWidth, nHeight), 8, 1);
	ASSERT(skinImg != NULL);
	iplSet(skinImg, 0);

	// step 1 == skin detection
	double dfSkinRatio = 0.0;
	dfSkinRatio = SkinDet(srcImg, skinImg);	

	if(bTest == TRUE) {
		cvNamedWindow("skin", 1);
		cvShowImage("skin", skinImg);
	}

//	cvSaveImage("E:\\skin.bmp", skinImg);

	// step 2 == morphological filtering
	IplImage* morphImg = cvCreateImage(cvSize(nWidth, nHeight), 8, 1);
	iplSet(morphImg, 0);

	int nIterations = 1;
//	iplClose(skinImg, morphImg, nIterations);
	iplOpen(skinImg ,morphImg,  nIterations);
	//cvErode(segImg, morphImg, NULL, 0);	


	// step 3 == skin region texture validation
	int BlockSize = 8;	// 估计窗口的大小
	// 设定边缘区为0
	for(row = 0; row < BlockSize; row++) {
		// top
		for(col = 0; col < nWidth; col++) {
			*(morphImg->imageData + row*morphImg->widthStep + col) = 0;
		}
		// bottom
		for(col = 0; col < nWidth; col++) {
			*(morphImg->imageData + (nHeight-1-row)*morphImg->widthStep + col) = 0;
		}
	}
	for(row = BlockSize; row < nHeight-BlockSize; row++) {
		// left
		for(col = 0; col < BlockSize; col++) {
			*(morphImg->imageData + row*morphImg->widthStep + col) = 0;
		}
		// right
		for(col = nWidth-BlockSize-1; col < nWidth; col++) {
			*(morphImg->imageData + row*morphImg->widthStep + col) = 0;
		}
	}

	if( bTest == TRUE ) {
		cvNamedWindow("morph", 1);
		cvShowImage("morph", morphImg);
	}

//	cvSaveImage("E:\\skinMorph.bmp", morphImg);

	// 轮廓跟踪,椭圆拟合
	// 几个条件限制门限
	CvRect	rect;	// 区域包络矩形
	double	sh_solidity, sh_eccent;	//sh_compact, 
	double area		= 0.0;	// 区域面积
	double perim	= 0.0;	// 周长
	double cent_x, cent_y;
	double meanGray = 0.0;

	int		th_size_min = min(50, nHeight/5);	// 最小矩形尺寸 (不小于此)
	int		th_size_max = 2*nHeight/3;	// 最大矩形尺寸 (不大于此)
	double	th_compact = 0.25;	// compact = area/(4*PI*p*p), 不小于此
	double	th_solidity = 0.4;	// solidity = area / (Dx*Dy), 不小于此
	double  th_eccent_max = 2.5;	// eccentrity = Dy/Dx, 不大于此
	double  th_eccent_min = 0.4;	// eccentrity = Dy/Dx, 不小于此

	CvMemStorage* storage = 0;
	storage = cvCreateMemStorage(0);
	CvSeq* contours = 0;	
    CvSeq* result;
	CvMoments* moments = new CvMoments;	// 区域矩特征
	CvPoint* pt = 0;

  	// 轮廓分析后图像数据已经改变,所以要求保持原数据,同时拷贝合理数据区域
	IplImage* tmpImg = cvCreateImage( cvSize(nWidth, nHeight), 8, 1 ); 
	cvCopyImage(morphImg, tmpImg);
	// 只求满足条件的最大面积的人头区
	CArray<HEAD, HEAD&> arrHead;
	arrHead.RemoveAll();
	HEAD aHead;

    // select the maximum ROI in the image  
    cvSetImageROI( tmpImg, cvRect( 0, 0, nWidth, nHeight ));
      
    // find contours and store them all as a list  
	int ContourNum=cvFindContours( tmpImg, storage, &contours, sizeof(CvContour),
            CV_RETR_LIST, CV_CHAIN_APPROX_NONE );
            
    while( contours )     {
		result = cvApproxPoly( contours, sizeof(CvContour), storage,
                    CV_POLY_APPROX_DP, cvContourPerimeter(contours)*0.02, 0 );
		area = fabs(cvContourArea(result,CV_WHOLE_SEQ));
		rect  = cvContourBoundingRect( result, 0);
		perim = cvContourPerimeter(contours);

		// 逐层剔除掉不符条件的区域 ==================
		// cont.1 -- size cond
		if( rect.height < th_size_min || rect.width < 4*th_size_min/5 || max(rect.width, rect.height) > th_size_max) {
			contours = contours->h_next; 
			continue;
		}

		/*	
		// cont.2 -- compact cond 由于肤色提取不佳,暂时不用
		sh_compact = 4*3.1415926*area / (perim*perim);
		if( sh_compact < th_compact ) {
			contours = contours->h_next; 
			continue;
		}
		*/

		// cont.3 -- solidity cond
		sh_solidity = (double)area/(rect.width*rect.height) * (4 / 3.1415926);
		if( sh_solidity < th_solidity ) {
			contours = contours->h_next; 
			continue;
		}

		// cont.4 -- eccent cond
		sh_eccent = (double)rect.height/rect.width;
		if( sh_eccent < th_eccent_min || sh_eccent > th_eccent_max ) {
			contours = contours->h_next; 
			continue;
		}

		// cont.5 -- 内部为黑色(不是球区),不符条件
		//cvSetImageROI( fieldImg, rect );
		//meanGray = cvMean(fieldImg, 0);	// 这样计算颜色均值不可靠,原因不明
		meanGray = 0.0;
		for(ii = rect.y; ii < rect.y+rect.height; ii++) {
			for(jj = rect.x; jj < rect.x+rect.width; jj++) {
				//iplGetPixel(fieldImg, jj, ii, tmpPixel);
				// data bytes aligned with bytes that can divided by 4, actual bytes per line is "widthStep"
				meanGray += (BYTE)(morphImg->imageData[ii*morphImg->widthStep + jj]);
				//tmpGray  += (BYTE)(fieldImg->imageData[ii*nWidth + jj]);
			}
		}
		meanGray /= (rect.height*rect.width);
		if(meanGray < 100)	{// 区域内主要为0,不处理
			contours = contours->h_next; 
			continue;
		}

		// (1)== 计算圆形性参数
		cvContourMoments(result, moments);
		cent_x = moments->m10/moments->m00;
		cent_y = moments->m01/moments->m00;

		// 记录人头区域的信息
		aHead.cent_x = (int)cent_x;
		aHead.cent_y = (int)cent_y;
		aHead.radi_x = rect.width/2;
		aHead.radi_y = rect.height/2;
		// 区域不能超出图像范围
		if( aHead.cent_x-aHead.radi_x >= 0 && aHead.cent_x+aHead.radi_x < nWidth &&\
			aHead.cent_y-aHead.radi_y >= 0 && aHead.cent_y+aHead.radi_y < nHeight ) {
			arrHead.Add(aHead);
		}
	  
		contours = contours->h_next;    
    }

	delete moments;	moments = NULL;
	cvClearMemStorage( storage );
	cvReleaseMemStorage (&storage);

	// 选择检测到的人头区域纵面积最大的一个
	int max_area = 0;
	int max_index = 0;
	HEAD max_Head;
	if(arrHead.GetSize() >= 1) {		
		
		for(ii=0; ii < arrHead.GetSize(); ii++) {
			aHead = arrHead.GetAt(ii);
			if( aHead.radi_x * aHead.radi_y >= max_area ) {
				max_area = aHead.radi_x * aHead.radi_y;
				max_index = ii;
			}
		}
		max_Head = arrHead.GetAt(max_index);


		// 评价候选区域的纹理特性
		CRect rectROI;
		rectROI.left	= max( (int)max_Head.cent_x - max_Head.radi_x, 0 );
		rectROI.right	= min( (int)max_Head.cent_x + max_Head.cent_x, srcImg->width-1);
		rectROI.top		= max( (int)max_Head.cent_y - max_Head.radi_y, 0 );
		rectROI.bottom	= min( (int)max_Head.cent_y + max_Head.radi_y, srcImg->height-1);
		tagTexture	txtu;
		ImgTexture(srcImg, rectROI, txtu, 1, 1);

		double th_head_cont = 25.0;
		if( txtu.CONT < th_head_cont ) 
			bHead = TRUE;

		if(bHead && bTest) {	// 测试数据时才画出人头区
			//	画椭圆
			cvEllipse( dstImg, cvPoint((int)max_Head.cent_x, (int)max_Head.cent_y), cvSize(max_Head.radi_x, max_Head.radi_y), 0,
			0, 360, CV_RGB(255,0,0), 3 );
		}
	}

//	cvSaveImage("E:\\head.bmp", dstImg);

	cvReleaseImage(&skinImg);		
	cvReleaseImage(&morphImg);		
	cvReleaseImage(&tmpImg);

	return bHead;
}

/*
*	纹理统计信息
*	input: RGB image
*	IplImage* iplImg		-- 原始彩色图像
*	tagTexture& txtu		-- 纹理数据
*	int delta_d				-- 共生矩阵中像素的统计距离间隔 d
*	int pixel_step			-- 共生矩阵中像素统计步长距离间隔 d
*/
void CShotClassifier::ImgTexture(IplImage* iplImg,  CRect& ROI, tagTexture& txtu, int delta_d, int pixel_step)
{
/*	
	// test 2 : 边缘密度	-------> 门限值为 0.20 比较好
	double dfEdgeDensity = 0.0;

	IplImage* grayImg = cvCreateImage(cvSize(iplImg->width, iplImg->height), 8, 1);
	IplImage* edgeImg = cvCreateImage(cvSize(iplImg->width, iplImg->height), 8, 1);
	cvCvtColor(iplImg, grayImg, CV_BGR2GRAY);
	iplSet(edgeImg, 0);


	cvCanny(grayImg, edgeImg, 50.0f, 150.0f, 3);

	// ==感兴趣区域
	CRect dstROI;
	dstROI.left = (int)(0.2 * iplImg->width);
	dstROI.top  = (int)(0.2 * iplImg->height);
	dstROI.right = (int)(0.8 * iplImg->width);
	dstROI.bottom= (int)(0.8 * iplImg->height);

	// == 统计边缘点数
	int r,c;
	for(r=dstROI.top; r < dstROI.bottom; r++) {
		for(c=dstROI.left; c < dstROI.right; c++) {
			if( (BYTE)(*(edgeImg->imageData + r*edgeImg->widthStep + c)) >= 128 )
				dfEdgeDensity ++;
		}
	}

	dfEdgeDensity = dfEdgeDensity/(dstROI.Height()*dstROI.Width());

	cvNamedWindow("gray", CV_WINDOW_AUTOSIZE);
	cvShowImage("gray", grayImg);
	cvNamedWindow("edge", CV_WINDOW_AUTOSIZE);
	cvShowImage("edge", edgeImg);

	cvReleaseImage(&grayImg);
	cvReleaseImage(&edgeImg);

	return dfEdgeDensity;
*/

	// == test 3: 共生矩阵方式
	IplImage* grayImg = cvCreateImage(cvSize(iplImg->width, iplImg->height), 8, 1);
	cvCvtColor(iplImg, grayImg, CV_BGR2GRAY);

	// == 灰度量化级别
	int nGryLevel = 64;
	int nGryStep = 256/64;

	// == 距离
//	int delta_d = 2;		// 统计距离
//	int pixel_step = 2;		// 统计步长

	CMatrix<double> GryCoMatrix(nGryLevel, nGryLevel);

	int r,c;
	BYTE gry_cur;		// 当前点灰度
	BYTE gry_right, gry_downright, gry_down, gry_downleft, gry_left, gry_upleft, gry_up, gry_upright;	// 周围四点的灰度
	for( r = delta_d; r < ROI.Height()-delta_d; r += pixel_step) {
		for( c = delta_d; c < ROI.Width()-delta_d; c += pixel_step) {
			gry_cur = ( (BYTE)(*(grayImg->imageData + r*grayImg->widthStep + c)) ) / nGryStep; 
			
			gry_right = ( (BYTE)(*(grayImg->imageData + r*grayImg->widthStep+ (c+delta_d) )) ) / nGryStep; 
			gry_downright = ( (BYTE)(*(grayImg->imageData + (r+delta_d)*grayImg->widthStep + (c+delta_d))) ) / nGryStep; 
			gry_down = ( (BYTE)(*(grayImg->imageData + (r+delta_d)*grayImg->widthStep + c)) ) / nGryStep; 
			gry_downleft = ( (BYTE)(*(grayImg->imageData + (r+delta_d)*grayImg->widthStep + (c-delta_d))) ) / nGryStep; 
			gry_left = ( (BYTE)(*(grayImg->imageData + r*grayImg->widthStep + (c-delta_d))) ) / nGryStep; 
			gry_upleft = ( (BYTE)(*(grayImg->imageData + (r-delta_d)*grayImg->widthStep + (c-delta_d))) ) / nGryStep; 
			gry_up = ( (BYTE)(*(grayImg->imageData + (r-delta_d)*grayImg->widthStep + c)) ) / nGryStep; 
			gry_upright = ( (BYTE)(*(grayImg->imageData + (r-delta_d)*grayImg->widthStep + (c+delta_d))) ) / nGryStep; 

			GryCoMatrix[gry_cur][gry_right] +=1.0;
			GryCoMatrix[gry_cur][gry_downright] +=1.0;
			GryCoMatrix[gry_cur][gry_down] +=1.0;
			GryCoMatrix[gry_cur][gry_downleft] +=1.0;
			GryCoMatrix[gry_cur][gry_left] +=1.0;
			GryCoMatrix[gry_cur][gry_upleft] +=1.0;
			GryCoMatrix[gry_cur][gry_up] +=1.0;
			GryCoMatrix[gry_cur][gry_upright] +=1.0;

		}
	}

	// 规一化
	double dfSum = 0.0;
	for(r=0; r < GryCoMatrix.GetRow(); r++) {
		for(c=0; c < GryCoMatrix.GetCol(); c++) {
			dfSum = dfSum + GryCoMatrix[r][c];
		}
	}

	for(r=0; r < GryCoMatrix.GetRow(); r++) {
		for(c=0; c < GryCoMatrix.GetCol(); c++) {
			GryCoMatrix[r][c] = GryCoMatrix[r][c]/dfSum;
		}
	}

	// 角二阶距
	txtu.ASM = 0.0;
	for(r = 0; r < nGryLevel; r++) {
		for(c = 0; c < nGryLevel; c++) {
			txtu.ASM += (GryCoMatrix[r][c]*GryCoMatrix[r][c]);
		}
	}

	// 对比度
	txtu.CONT = 0.0;
	int n;
	for(n = 1; n < nGryLevel; n++) {
		for(r = 0; r < nGryLevel; r++) {
			for(c = 0; c < nGryLevel; c++) {
				if( abs(r-c) == n )
					txtu.CONT += (n*n*GryCoMatrix[r][c]);
			}
		}
	}

	// 熵
	txtu.ENTROPY = 0.0;
	for(r = 0; r < nGryLevel; r++) {
		for(c = 0; c < nGryLevel; c++) {
			if( GryCoMatrix[r][c] > 0.0001 )
				txtu.ENTROPY += (-GryCoMatrix[r][c]*log10(GryCoMatrix[r][c]));
		}
	}

	cvReleaseImage(&grayImg);
	GryCoMatrix.DeleteMatrix();
}

/* 估计场地与非场地边缘线的倾斜角度(并拟合),输入场地分割图像,输入线条的参数
*	IplImage* fieldImg	-- 已经分割好的场地图像(场地用1表示)
*	double& delta_x		-- 场地线参数, 角度 delta_x, 右向为正
*	double& delta_y		-- 场地线参数, 角度 delta_y, 右下为正
*	int& pt_x			-- 直线上的一点的x坐标
*	int& pt_y			-- 直线上的一点的y坐标
*	int line_flag		-- 拟合方法(0 -- linefit, 1 -- hough transform
*/
void CShotClassifier::FieldEdgeFit(IplImage* fieldImg, double& delta_x, double& delta_y, int& pt_x, int& pt_y, int line_flag)
{
	int row, col;
	
	int nHeight = fieldImg->height;
	int nWidth  = fieldImg->width;

	int margin = 5;	// 边缘空白处不统计
	CMatrix<int> HorProj(1, nWidth-2*margin);

	int Sum = 0;
	BYTE pixel = 0;
	for(col = margin; col < nWidth-margin; col++)   {      
		Sum = 0;
		pixel = 0;
		for(row = 0; row < nHeight; row++) {
			pixel = (BYTE)(*(fieldImg->imageData + row*fieldImg->widthStep + col));
			Sum += (pixel/255);
		}

		HorProj[0][col-margin] = abs(nHeight-1-Sum);	
	} 

	// 中(均)值滤波一下,可能效果会更好一些
	MedianFilter(HorProj, 1, 5, 2, 0);

	// step 2: generate field horizontal project image
	if(line_flag == 0)	{	// line fit	
		CvPoint2D32f* PointArray2D32f;
		PointArray2D32f = new CvPoint2D32f[nWidth-2*margin];
		
		for(col = 0; col < nWidth - 2*margin; col++) {	
			PointArray2D32f[col].x = (float)(col+margin);
			PointArray2D32f[col].y = (float)(HorProj[0][col]);
		}

		float reps = 0;
		float aeps = 0;
		float *line = new float[4];
		cvFitLine2D(PointArray2D32f, nWidth-2*margin, CV_DIST_L2, NULL, reps, aeps, line);
	/*	line -- Pointer to the array of four floats. When the function exits, the first
		two elements contain the direction vector of the line normalized to 1,
		the other two contain coordinates of a point that belongs to the line.	
		tan(theta) = line[1]/line[0], 水平逆时针为负,顺时针向下为正
		*/

		delta_x = (double)(line[0]);
		delta_y = (double)(line[1]);
		pt_x = (int)(line[2]);
		pt_y = (int)(line[3]);

		delete []line;
		delete []PointArray2D32f;
	}
	else if(line_flag == 1)	{ // hough transform
		// Horproject Image
		IplImage* projImg = cvCreateImage(cvSize(nWidth, nHeight), 8, 1);
		ASSERT(projImg != NULL);
		iplSet(projImg, 0);

		for(col = 0; col < nWidth - 2*margin; col++) {	
			*(projImg->imageData + HorProj[0][col]*projImg->widthStep + col+margin) = (BYTE) 255;
			//iplPutPixel(projImg, col+margin, HorProj[0][col], (BYTE *)whitejPxl);		
		}
		
//		cvNamedWindow("proj", 1);
//		cvShowImage("proj", projImg);

		// Hough line detection
		LINE aLine;
		aLine.rho = 0.0;
		aLine.theta = 0.0;
		BOOL bLineDet = MyHoughDet(projImg, aLine);
		// 将rho和theta统一到与opencv相同的定义上
		aLine.theta = aLine.theta*PI/180;
		if(aLine.theta < 0) {
			aLine.rho = -aLine.rho;
			aLine.theta = PI + aLine.theta;
		}
		delta_x = cos(aLine.theta - PI/2);
		delta_y = sin(aLine.theta - PI/2);
	
		double a = cos(aLine.theta), b = sin(aLine.theta);
		if( fabs(b) < 0.001 )
		{
			pt_x = cvRound(aLine.rho);
			pt_y = 0;		
		}
		else if( fabs(a) < 0.001 )
		{
			pt_y = cvRound(aLine.rho);
			pt_x = 0;			
		}
		else
		{
			if( aLine.rho >= 0 ) {
				pt_x = 0;
				pt_y = cvRound(aLine.rho/b);			
			}
			else {
				pt_y = 0;
				pt_x = cvRound(aLine.rho/b);
			}       
		}

		cvReleaseImage(&projImg);
	}

	HorProj.DeleteMatrix();
}

/*
*	
*/
BOOL CShotClassifier::MyHoughDet(IplImage* edgeImg, LINE& aLine)
{
	int i,j;
	CString strMsg;
	
//	IplImage* color_dst = cvCreateImage( cvGetSize(edgeImg), 8, 3 );
	//IplImage* dst = cvCreateImage( cvGetSize(src), 8, 1 );
//	cvCvtColor( src, color_dst, CV_GRAY2BGR );
	//cvCanny( src, dst, 50, 200, 3 );	
    //cvCvtColor( dst, color_dst, CV_GRAY2BGR );

	int nHeight = edgeImg->height;
	int nWidth  = edgeImg->width;


	// =====================================================================================
	// rho -- distance between original point and line (vertical distance)
	// theta -- angle between horiztonal line to the vertical line form original point to line
	//			(positive if wise-hand, negative i case of counterwise-hand)
	int nRho, nTheta;
	// dis -- initial distance between the current point to original point
	// ang -- initial angle between the horizontal line to line connecting original point and current point
	double dis, ang;
	double dTheta;

	// rho and theta resulation
	int rho_res, theta_res;
	rho_res = 1;
	theta_res = 1;

	// real max rho and theta
	int max_rho_real, max_theta_real;
	max_rho_real = (int)(sqrt(nHeight*nHeight + nWidth*nWidth) + 0.5);
	max_theta_real = 270;	//180;

	// max-rho and max-theta for index
	int max_rho_index, max_theta_index;
	max_rho_index   = max_rho_real/rho_res;
	max_theta_index = max_theta_real/theta_res;

	CMatrix<DWORD> dwAccu(max_rho_index, max_theta_index);

	for(i=0; i<nHeight; i++) {
		for(j=0; j<nWidth; j++) {
			// only operation on white pixel (1 gray level)
			if( (BYTE)(*(edgeImg->imageData + edgeImg->widthStep*i + j)) >= 128 ) {
				dis = sqrt(i*i + j*j);
				if( j == 0 )	// the angle between horizontal line and the line connecting current and p(0,0) is 90 degree
					ang = 90;//PI / 2;
				else {
					ang = atan2((double)i,(double)j) * 180 / PI;	// radian -> angle
				}
				
				//for(nTheta = -89; nTheta <= 179; nTheta++) {
				//	dTheta = (ang - nTheta) / 180 * PI;	// angle -> radian
				//	nRho   = (int) ( fabs(dis * cos(dTheta)) + 0.5 );
				//	dwAccu[nRho][nTheta+89]++;	// real angle -> index
					// combine dwAccu[rho][0] with dwAccu[rho][179],set dwAccu[rho][0] be 0
					//dwAccu[nRho][179] += dwAccu[nRho][0];	dwAccu[nRho][0] = 0;
				//}
				
				for(nTheta = (int)(ang-89); nTheta <= (int)(ang+90); nTheta++) {
					dTheta = (ang - nTheta) / 180 * PI;	// angle -> radian
					nRho   = (int) ( fabs(dis * cos(dTheta)) + 0.5 );
					dwAccu[nRho/rho_res][(nTheta+89)/theta_res]++;	// real angle -> index
					// combine dwAccu[rho][0] with dwAccu[rho][179],set dwAccu[rho][0] be 0
					//dwAccu[nRho][179] += dwAccu[nRho][0];	dwAccu[nRho][0] = 0;
				}
			}
		}
	}
	
	// thresholds
	DWORD th_accu = 20;	// threshold of accumulative array
	int rho_gap  = 10;	// minimum distance gap between two adjacent -lines
	int theta_gap = 10; // minimum angle gap between two adjacent -lines

	// == find lines in the accumulative array
	bool bFound = true;// whether there exist lines; true -- exist, else no lines
	DWORD nMax = 0;	
	int ii, jj;
	int left_rho, right_rho, left_theta, right_theta;

	//int line_count = 1;
	while(bFound/*line_count--*/) {
		bFound = false;	
		nMax = 0;	
		for(i=0; i<max_rho_index; i++) {
			for(j=0; j<max_theta_index; j++) {
				if(nMax < dwAccu[i][j]) {
					nMax = dwAccu[i][j];
					nRho = i;
					nTheta = j;
				}
			}
		}

		nMax = nMax;

		if(nMax >= th_accu*rho_res*theta_res) {
			bFound = true;			
			aLine.rho   = nRho * rho_res;
			aLine.theta = (nTheta * theta_res - 89);
			
			// clear data near this point
			left_rho	= (nRho - rho_gap/rho_res/2 > 0) ? (nRho - rho_gap/rho_res/2) : 0;
			right_rho   = (nRho + rho_gap/rho_res/2 < max_rho_index) ? (nRho + rho_gap/rho_res/2) : max_rho_index;
			left_theta	= (nTheta - theta_gap/theta_res/2 > 0) ? (nTheta - theta_gap/theta_res/2) : 0;
			right_theta	= (nTheta + theta_gap/theta_res/2 < max_theta_index) ? (nTheta + theta_gap/theta_res/2) : max_theta_index;
			for(ii = left_rho; ii <= right_rho; ii++) {
				for(jj = left_theta; jj <= right_theta; jj++) {
					dwAccu[ii][jj] = 0;
				}
			}
		}
		break;	// 最多一根直线 midified by tong 2003.11.26
	}

//	nTheta -= 89;	// angle index -> real angle
//	dTheta = (double)nTheta/180 * PI;

	dwAccu.DeleteMatrix();

	return bFound;
}

// 肤色区域检测,返回肤色面积大小,输入是RGB彩色图像, 结果保存于skinImg,白色为肤色
// 加上肤色区域的形状,位置等条件限制
double CShotClassifier::SkinDet(IplImage* srcImg, IplImage* skinImg)
{
	double dfSkinArea = 0.0;
	int row, col;
	
	int nHeight = srcImg->height;
	int nWidth  = srcImg->width;

	BYTE red, green, blue;
	BOOL bSkin = FALSE;
	for(row = 0; row < nHeight; row++) {
		for(col = 0; col < nWidth; col++) {
			//iplGetPixel(iplImg, col, row, (BYTE *)(un_pixel));	// BGR mode
			
			blue	= (BYTE)(*(srcImg->imageData + row*srcImg->widthStep + 3*col));
			green	= (BYTE)(*(srcImg->imageData + row*srcImg->widthStep + 3*col+1));
			red		= (BYTE)(*(srcImg->imageData + row*srcImg->widthStep + 3*col+2));			
			
			bSkin = SkinSeg(red, green, blue);

			if( bSkin ) {				
				skinImg->imageData[row*skinImg->widthStep + col] = (BYTE)(255);
				dfSkinArea += 1.0;
			}			
		}
	}
	dfSkinArea /= (nHeight*nWidth);
/*
	cvNamedWindow("skin", CV_WINDOW_AUTOSIZE);
	cvShowImage("skin", skinImg);
*/

	return dfSkinArea;
}

// 肤色区域分割
BOOL CShotClassifier::SkinSeg(BYTE red, BYTE green, BYTE blue)
{
/*
	r = R/(R+G+B)
	g = G/(R+G+B)

	dif = abs([r g] - rgmiu] 
	probability = exp(dif * inv_cov_d2 * dif);              % likelyhood
	threshold = 0.1
*/
	// step 1: 首先用亮度值滤除一部分
	int th_low = 20;
	int th_high = 210;

	int meanGry = 0;
	meanGry = (3*red + 6*green + blue) / 10;
	if( meanGry < th_low || meanGry > th_high )
		return FALSE;

	// step 2: rg model,多变量,单高斯
	double r,g;
	r = (double)(red)/(red+green+blue);
	g = (double)(green)/(red+green+blue);
	double mean[2] ={0.44753, 0.31908};

	double dif[2];
	double inv_cov[2][2];
	dif[0] = fabs(r-mean[0]);	dif[1] = fabs(g-mean[1]);
	inv_cov[0][0]	= -154.15;	inv_cov[0][1]	= -179.86;
	inv_cov[1][0]	= -179.86;	inv_cov[1][1]	= -791.76;

	double tmpDif[2];
	tmpDif[0] = dif[0]*inv_cov[0][0] + dif[1]*inv_cov[1][0];
	tmpDif[1] = dif[0]*inv_cov[0][1] + dif[1]*inv_cov[1][1];

	double index;
	index = tmpDif[0]*dif[0] + tmpDif[1]*dif[1];

	double prob = 0;
	prob = exp(index);

	double threshold = 0.1;
	if(prob > threshold)
		return TRUE;
	else
		return FALSE;

}