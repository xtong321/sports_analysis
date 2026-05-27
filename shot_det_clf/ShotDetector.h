// ShotDetector.h: interface for the CShotDetector class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_SHOTDETECTOR_H__49AB3808_09AD_4C70_A89E_5BC6B3D9A1A7__INCLUDED_)
#define AFX_SHOTDETECTOR_H__49AB3808_09AD_4C70_A89E_5BC6B3D9A1A7__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

class CShotDetector  
{	
public:
	DWORD	m_dwCurFrmNo;

public:
	CShotDetector();
	virtual ~CShotDetector();

	void	ImgHist(char *pDIB, int nHeight, int nWidth, DWORD *Hist, int nBin, int nChannel);
	DWORD	HistDiff(DWORD *CurHist, DWORD *RefHist, int nHeight, int nWidth, int nBin, int nChannel);
	DWORD   FrmDiff(char *pCurImgBuf, char *pRefImgBuf, int nHeight, int nWidth);

	// ¾µÍ·±ßÔµ¼ì²â
	bool	ShotDet(HWND hWnd, CString& strVideoPathName, DWORD dwSegStartFrm, DWORD dwSegEndFrm, 
					DWORD dwVideoFrmTotal, double dfVideoTimeTotal, 
					double dfFrameRate, double dfTimePerFrm, long lVidStrmIdx,
					int nVideoHeight, int nVideoWidth, long	lImgBufSize);

	};

#endif // !defined(AFX_SHOTDETECTOR_H__49AB3808_09AD_4C70_A89E_5BC6B3D9A1A7__INCLUDED_)
