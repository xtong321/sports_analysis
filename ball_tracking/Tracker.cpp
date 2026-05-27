// Tracker.cpp: implementation of the CTracker class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "vballtrack.h"
#include "Tracker.h"

#include <math.h>

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif

/////////////////////////////////////////////////////////////////////////////
// CTrackView

//#define BIN_MAX 8
#define PI		3.14159265358

double uniform_random(void)
{	
	return (double) rand() / (double) RAND_MAX;
}

double gaussian_random(void)
{
	static int next_gaussian = 0;
	static double saved_gaussian_value;
	
	double fac, rsq, v1, v2;
	
	if (next_gaussian == 0)
	{
		do
		{
			v1 = 2.0*uniform_random()-1.0;
			v2 = 2.0*uniform_random()-1.0;
			rsq = v1*v1+v2*v2;
		}
		while (rsq >= 1.0 || rsq == 0.0);

		fac = sqrt(-2.0*log(rsq)/rsq);
		saved_gaussian_value=v1*fac;
		next_gaussian=1;
		return v2*fac;
	}
	else
	{
		next_gaussian=0;
		return saved_gaussian_value;
	}
}

void Gaussian2D(double* x, double* y)
{
	double C[2][2] = {{3.7501, 1.7171},{0, 3.6466}};
	double u1 = gaussian_random();
	double u2 = gaussian_random();
	*x = 0.09+C[0][0]*u1;
	*y = 0.01+C[0][1]*u1+C[1][1]*u2;
}
//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CTracker::CTracker()
{
	m_bCondensation	= false;;
	m_bInitialized	= false;

	m_pModelHist	= NULL;
	m_pNewHist		= NULL;

	g_MaxWeight		= 0.0f;
	
//	m_CurIplImage	= NULL;
//	m_CurPoint = CPoint(0,0);
//	m_ObjRect.SetRect(0,0,0,0);
}

CTracker::~CTracker()
{
	m_bCondensation	= false;;
	m_bInitialized	= false;

	if( m_pModelHist != NULL ) {
		delete []m_pModelHist;
		m_pModelHist = NULL;
	}
	if( m_pNewHist != NULL ) {
		delete []m_pNewHist;
		m_pNewHist = NULL;
	}
}

void CTracker::InitConDenSation(int SampleTimes, CPoint m_CurPoint)
{
	m_bCondensation = TRUE;

	m_data.new_positionsX = (double *)malloc(sizeof(double) * SampleTimes);
	m_data.old_positionsX = (double *)malloc(sizeof(double) * SampleTimes);

	m_data.new_positionsY = (double *)malloc(sizeof(double) * SampleTimes);
	m_data.old_positionsY = (double *)malloc(sizeof(double) * SampleTimes);	

	m_data.sample_weights = (double *)malloc(sizeof(double) * SampleTimes);
	m_data.cumul_prob_array = (double *)malloc(sizeof(double) * SampleTimes);

	/* This is the initial positions . */
	m_data.meas.x = m_CurPoint.x;
	m_data.meas.y = m_CurPoint.y;

	for(int n=0; n<SampleTimes; n++)
	{
		double X,Y;
		Gaussian2D(&X,&Y);

		m_data.old_positionsX[n] = m_data.meas.x + X;
		m_data.old_positionsY[n] = m_data.meas.y + Y;
		
		/* The probabilities are not normalised. */
		m_data.cumul_prob_array[n] = (double) n;
		m_data.sample_weights[n] = 1.0;
	}

	m_data.largest_cumulative_prob = (double) n;
}

void CTracker::ClearConDenSation()
{
/*	free(m_data.new_positionsX);
	free(m_data.old_positionsX);

	free(m_data.new_positionsY);
	free(m_data.old_positionsY);

	free(m_data.sample_weights);
	free(m_data.cumul_prob_array);
*/
	delete [](m_data.new_positionsX);		m_data.new_positionsX = NULL;
	delete [](m_data.old_positionsX);		m_data.old_positionsX = NULL;

	delete [](m_data.new_positionsY);		m_data.new_positionsY = NULL;
	delete [](m_data.old_positionsY);		m_data.old_positionsY = NULL;

	delete [](m_data.sample_weights);		m_data.sample_weights = NULL;
	delete [](m_data.cumul_prob_array);		m_data.cumul_prob_array = NULL;

	m_bCondensation = false;
}
/*
  本函数实现Condensation算法.
  输入上一个目标坐标点和采样点个数,
  返回本帧图像中目标坐标点.
*/
CPoint CTracker::ConDenSation(CPoint OldPt, int SampleTimes, IplImage* grayImg, CRect ObjRect)
{
	/* Push previous state through process model */
	int n, base;
	for (n=0; n<SampleTimes; ++n)
	{
		//按权值大小产生新的样本点
		base = pick_base_sample(SampleTimes);
		double X,Y;
		Gaussian2D(&X,&Y);

		//对新样本点进行随机性漂移
		m_data.new_positionsX[n] = m_data.old_positionsX[base] + X;
		m_data.new_positionsY[n] = m_data.old_positionsY[base] + Y;
	}

	//measure
	double cumul_total = 0.0;
	double max_weight=0;
	double min_weight=1;
	int index;
	for (n=0; n<SampleTimes; ++n)
	{
		//测量每个样本点与观测点的相似性度量
		m_data.sample_weights[n] = evaluate_observation_density(n, grayImg, ObjRect);

		//保留权值最大值和对应采样点的索引值
		if(max_weight < m_data.sample_weights[n])
		{
			max_weight = m_data.sample_weights[n];
			index = n;
		}
		if(min_weight > m_data.sample_weights[n])
		{
			min_weight = m_data.sample_weights[n];
		}
		m_data.cumul_prob_array[n] = cumul_total;
		cumul_total += m_data.sample_weights[n];
	}
	m_data.largest_cumulative_prob = cumul_total;

	double X = 0;
	double Y = 0;
	X = m_data.new_positionsX[index];
	Y = m_data.new_positionsY[index];

	//将最大相似度值保存到全局变量中.
	g_MaxWeight = float(m_data.sample_weights[index]);

	//update
	for (n=0; n<SampleTimes; ++n)
	{
		m_data.old_positionsX[n] = X;
		m_data.old_positionsY[n] = Y;
	}

	return CPoint(int(X),int(Y));
}

/*
  按照采样点的权值来确定每个采样点在本次采样中被采样的概率
*/
int CTracker::pick_base_sample(int SampleTimes)
{
	double choice = uniform_random() * m_data.largest_cumulative_prob;
	int low, middle, high;
	
	low = 0;
	high = SampleTimes;
	
	while (high>(low+1))
	{
		middle = (high+low)/2;
		if (choice > m_data.cumul_prob_array[middle])
			low = middle;
		else
			high = middle;
	}
	
	return low;
}

/*
  通过直方图交叉(Histogram Intersection)得到对应采样点的相似性度量
*/
double CTracker::evaluate_observation_density(int n, IplImage* grayImg, CRect m_ObjRect)
{
	int X = int(m_data.new_positionsX[n]);
	int Y = int(m_data.new_positionsY[n]);

	CPoint NewPt = CPoint(X,Y);
	int iW = m_ObjRect.Width();
	int iH = m_ObjRect.Height();
	CRect rc = CRect(NewPt.x-iW/2,NewPt.y-iH/2,
					 NewPt.x+iW/2,NewPt.y+iH/2);
	if(rc.Height()%2 == 0)
		rc.bottom++;
	if(rc.Width()%2 == 0)
		rc.right++;

	// added by tong 2003.10.23
	rc.left		= (rc.left >= 0) ? (rc.left) : (0);
	rc.left		= (rc.left < grayImg->width) ? (rc.left) : (grayImg->width-1);
	rc.right	= (rc.right < grayImg->width) ? (rc.right) : (grayImg->width-1);
	rc.right	= (rc.right > 0) ? (rc.right) : (0);
	rc.top		= (rc.top  >= 0) ? (rc.top) : (0);
	rc.top		= (rc.top  < grayImg->height) ? (rc.top) : (grayImg->height-1);
	rc.bottom	= (rc.bottom < grayImg->height) ? (rc.bottom) : (grayImg->height-1);
	rc.bottom	= (rc.bottom > 0) ? (rc.bottom) : (0);
	
	if( rc.left >= grayImg->width-1 || rc.top >= grayImg->height-1 )
		return 0.0;
	// =============================================================================

	WeightHistgram(grayImg,m_pNewHist,rc,BIN_MAX);

	float NominateSum = 0;
	float DenominaSum = 0;
	for(int i=0; i<BIN_MAX; i++)
	{
		float minimum=0;
		if(*(m_pNewHist+i) <= *(m_pModelHist+i))
		{
			minimum = *(m_pNewHist+i);
		}
		else
		{
			minimum = *(m_pModelHist+i);
		}
		NominateSum += minimum;
		DenominaSum += *(m_pModelHist+i);
	}

	return double(NominateSum/DenominaSum);
}

void CTracker::WeightHistgram(IplImage* grayImg, float *hist,
								 CRect rect, int bin)
{
	for(int i=0; i<bin; i++)
	{
		*(hist+i) = 0;
	}

	float Hx = rect.Width()/float(2);
	float Hy = rect.Height()/float(2);
	float dis;
	CPoint pt;
	CPoint ptCn = rect.CenterPoint();
	char pixel[4] = {0,0,0,0};
	BYTE gray = 0;
	for( i=rect.top; i<=rect.bottom; i++)
	{
		for(int j=rect.left; j<=rect.right; j++)
		{
			pt = CPoint(j,i);
			dis = (pt.x-ptCn.x)*(pt.x-ptCn.x)/(Hx*Hx)+(pt.y-ptCn.y)*(pt.y-ptCn.y)/(Hy*Hy);
			if(dis>1)
				continue;

			//int gray = m_CurrentImage.GetGrayData(j,i);
			iplGetPixel(grayImg, j, i, pixel);
			gray = (BYTE)(pixel[0]);
			gray = gray/(256/bin);

			float kernel = KernelG(pt,ptCn,Hx,Hy);
			*(hist+gray) = *(hist+gray)+kernel;
		}
	}

	//normalize
	float sum = 0;
	for(i=0; i<bin; i++)
	{
		sum = sum + *(hist+i);
	}
	for(i=0; i<bin; i++)
	{
		*(hist+i) = *(hist+i) / sum;
	}
}

float CTracker::KernelG(CPoint data, CPoint origin, float Hx, float Hy)
{
	CSize size = data - origin;
	float temp = 1 - (size.cx*size.cx/(Hx*Hx) + size.cy*size.cy/(Hy*Hy));
	if(temp>=0)
	{
		return float(4.0f * temp/(2 * PI));
	}
	else
	{
		return 0;
	}
}