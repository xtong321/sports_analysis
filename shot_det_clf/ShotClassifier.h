// ShotClassifier.h: interface for the CShotClassifier class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_SHOTCLASSIFIER_H__894FFA82_90C1_4E5A_B185_61697B178631__INCLUDED_)
#define AFX_SHOTCLASSIFIER_H__894FFA82_90C1_4E5A_B185_61697B178631__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "iplwind.h"
#include "ipl.h"
#include "cv.h"
#include "highgui.h"
#include "Global.h"

class CShotClassifier  
{
public:
	DWORD	m_dwCurFrmNo;

public:
	CShotClassifier();
	virtual ~CShotClassifier();

	// 镜头分类
	int		ShotClassification(HWND hWnd, CString& strVideoPathName, DWORD dwShotStartFrm, DWORD dwShotEndFrm, 
							   DWORD dwVideoFrmTotal, double dfVideoTimeTotal, 
							   double dfFrameRate, double dfTimePerFrm, long lVidStrmIdx,
							   int nVideoHeight, int nVideoWidth, long	lImgBufSize);
	// 单个画面的分类,返回结果为画面的类别索引(与镜头类别对应)
	int		ViewClassification(IplImage* srcImg);

	// 是否存在场地区域
	BOOL	ExistField(IplImage* srcImg, double& grass_ratio);

	// 计算场地区域的面积比例
	double	FieldExtraction(IplImage* orgImg, IplImage* fieldImg);

	// 提取场地区域
	BOOL	FindFieldContour(IplImage* srcImg, IplImage* fieldImg, IplImage* fieldContImg, IplImage* dstImg, BOOL bTest);
	
	// 提取场地区域的凸轮廓
	BOOL	GetFieldConvexContour(IplImage* segImg, double dfFieldRatio, IplImage* fieldContImg, IplImage* dstImg, BOOL bTest);

	// 提取场地区域中的目标
	BOOL	ObjExtraction(IplImage* srcImg, IplImage* dstImg, OBJ_FIELD& obj_para, BOOL bTest);

	// 判断是否存在合格(一定尺度)的人头区域
	BOOL	HeadDet(IplImage* srcImg, IplImage* dstImg, BOOL bTest);

	// 计算图像的纹理特性
	void	ImgTexture(IplImage* iplImg,  CRect& ROI, tagTexture& txtu, int delta_d, int pixel_step);
	
	// 估计场地与非场地边缘线的倾斜角度(并拟合),输入场地分割图像,输入线条的参数
	void	FieldEdgeFit(IplImage* fieldImg, double& delta_x, double& delta_y, int& pt_x, int& pt_y, int line_flag);

	BOOL	MyHoughDet(IplImage* edgeImg, LINE& aLine);

	// 肤色区域检测,返回肤色面积大小,输入是RGB彩色图像, 结果保存于skinImg,白色为肤色
	double	SkinDet(IplImage* srcImg, IplImage* skinImg);
	
	// 肤色区域分割,判断某一点(RGB)是不是肤色区域
	BOOL	SkinSeg(BYTE red, BYTE green, BYTE blue);

};

#endif // !defined(AFX_SHOTCLASSIFIER_H__894FFA82_90C1_4E5A_B185_61697B178631__INCLUDED_)
