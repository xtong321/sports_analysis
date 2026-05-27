// SMRLogoDet.h: interface for the CSMRLogoDet class.
// 
//	自动检测 Slow-Motion Replay (SMR) 的标志图像(Logo)
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_SMRLOGODET_H__5830653A_08D1_45FF_BCA5_B71006B74F8A__INCLUDED_)
#define AFX_SMRLOGODET_H__5830653A_08D1_45FF_BCA5_B71006B74F8A__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "iplwind.h"
#include "ipl.h"
#include "cv.h"
#include "highgui.h"
#include "Global.h"

class CSMRLogoDet  
{
public:
	DWORD	m_dwCurFrmNo;

public:
	CSMRLogoDet();
	virtual ~CSMRLogoDet();

	// SMR_Logo模板的自动提取
	bool	SMRLogoTemplExtract(HWND hWnd, CString& strVideoPathName,
							DWORD dwVideoFrmTotal, double dfVideoTimeTotal, 
							double dfFrameRate, double dfTimePerFrm, long lVidStrmIdx,
							int nVideoHeight, int nVideoWidth, long	lImgBufSize);
	
	// 利用SMR_Logo模板提取整场比赛中的SMR_Logo
	bool	SMRLogoDet(HWND hWnd, CString& strVideoPathName, CString& strGameType, 
					   CString&	strLogoTemplPathName, /* char* pLogoTemplBuf, */
					   DWORD dwVideoFrmTotal, double dfVideoTimeTotal, 
					   double dfFrameRate, double dfTimePerFrm, long lVidStrmIdx,
					   int nVideoHeight, int nVideoWidth, long	lImgBufSize);
	bool	SMRLogoDet_ImgMatch(HWND hWnd, CString& strVideoPathName, CString& strGameType, 
					   CString&	strLogoTemplPathName, /* char* pLogoTemplBuf, */
					   DWORD dwVideoFrmTotal, double dfVideoTimeTotal, 
					   double dfFrameRate, double dfTimePerFrm, long lVidStrmIdx,
					   int nVideoHeight, int nVideoWidth, long	lImgBufSize);
	void    InVariantMoment(BYTE* pImgDIB, INVAR_MOMENT& moment);
	void	InVariantMoment(IplImage* img, INVAR_MOMENT& moment);
	double  ImgColorDiff(char* pRGBDIB1, char* pRGBDIB2, const char *strColor);

};

#endif // !defined(AFX_SMRLOGODET_H__5830653A_08D1_45FF_BCA5_B71006B74F8A__INCLUDED_)
