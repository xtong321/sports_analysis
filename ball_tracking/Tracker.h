// Tracker.h: interface for the CTracker class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_TRACKER_H__E8F08CF8_092F_4E9D_B764_AD901CA386C6__INCLUDED_)
#define AFX_TRACKER_H__E8F08CF8_092F_4E9D_B764_AD901CA386C6__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "ipl.h"

typedef struct _IterationData
{
  /* The following arrays contain the sample positions for the current
     and previous timesteps respectively. At the end of each
     iteration, these pointers are swapped over to avoid copying data
     structures, so their addresses should not be relied on. */
	double *new_positionsX, *old_positionsX;
	double *new_positionsY, *old_positionsY;

  /* The following arrays give the sample weights and cumulative
     probabilities, as well as the largest cumulative
     probability. There is no stage in the algorithm when the weights
     from both the previous and current timesteps are needed. At the
     beginning of an iteration, sample_weights contains the weights
     from the previous iteration, and by the end it contains the
     weights of the current iteration. The cumulative probabilities
     are not normalised, so largest_cumulative_prob is needed to store
     the largest cumulative probability (for simplicity of the binary
     search algorithm, cumul_prob_array[0] = 0) */
	double *sample_weights, *cumul_prob_array, largest_cumulative_prob;

  /* The measurements made in a given iteration are stored here. For
     some applications a discrete set of measurements is not
     appropriate, and this could contain, e.g. a pointer to an image
     structure. */
	CPoint meas;
} IterationData;

#define BIN_MAX 16

class CTracker  
{
public:
	BOOL		m_bCondensation;
	BOOL		m_bInitialized;
	IterationData	m_data;
	float*		m_pModelHist;
	float*		m_pNewHist;

	float		g_MaxWeight;

//	IplImage*	m_CurIplImage;		// ipl image
//	CPoint		m_CurPoint;
//	CRect		m_ObjRect;

public:
	CTracker();
	virtual ~CTracker();

	//CPoint ConDenSation(CPoint OldPt, int SampleTimes);
	CPoint ConDenSation(CPoint OldPt, int SampleTimes, IplImage* grayImg, CRect ObjRect);

	//void InitializeConDenSation(int SampleTimes);
	void InitConDenSation(int SampleTimes, CPoint m_CurPoint);
	void ClearConDenSation();

	int pick_base_sample(int SampleTimes);
	//double evaluate_observation_density(int n);
	double evaluate_observation_density(int n, IplImage* grayImg, CRect m_ObjRect);

	void WeightHistgram(IplImage* grayImg, float *hist, CRect rect, int bin);
	float KernelG(CPoint data, CPoint origin, float Hx, float Hy);

};

#endif // !defined(AFX_TRACKER_H__E8F08CF8_092F_4E9D_B764_AD901CA386C6__INCLUDED_)
