/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Written (W) 1999-2008 Gunnar Raetsch
 * Written (W) 2007-2009 Soeren Sonnenburg
 * Copyright (C) 1999-2009 Fraunhofer Institute FIRST and Max-Planck-Society
 */

#include <shogun/clustering/KMeans.h>
#include <shogun/distance/Distance.h>
#include <shogun/labels/Labels.h>
#include <shogun/features/DenseFeatures.h>
#include <shogun/mathematics/Math.h>
#include <shogun/base/Parallel.h>

#ifdef HAVE_PTHREAD
#include <pthread.h>
#endif

#define MUSRECALC

using namespace shogun;

CKMeans::CKMeans()
: CDistanceMachine()
{
	init();
}

CKMeans::CKMeans(int32_t k, CDistance* d, bool use_kmpp)
: CDistanceMachine()
{	
	init();
	m_k=k;
	set_distance(d);
	m_use_kmeanspp=use_kmpp;
}

CKMeans::CKMeans(int32_t k_i, CDistance* d_i, SGMatrix<float64_t> centers_i)
: CDistanceMachine()
{
	init();
	m_k = k_i;
	set_distance(d_i);
	set_initial_centers(centers_i);
}

CKMeans::~CKMeans()
{
}	

void CKMeans::set_initial_centers(SGMatrix<float64_t> centers)
{	
	
	m_dimensions = ((CDenseFeatures<float64_t>*) distance->get_lhs())->get_num_features();
	REQUIRE(centers.num_cols == m_k,
			"Expected %d initial cluster centers, got %d", m_k, centers.num_cols);
	REQUIRE(centers.num_rows == m_dimensions,
			"Expected %d dimensionional cluster centers, got %d", m_dimensions, centers.num_rows);
	m_mus_initial = centers;
}

void CKMeans::set_random_centers(SGVector<float64_t> weights_set, SGVector<int32_t> ClList, int32_t XSize)
{
	CDenseFeatures<float64_t>* lhs=
		CDenseFeatures<float64_t>::obtain_from_generic(distance->get_lhs());

	for (int32_t i=0; i<XSize; i++)
	{
		const int32_t Cl=CMath::random(0, m_k-1);
		weights_set[Cl]+=1.0;
		ClList[i]=Cl;

		int32_t vlen=0;
		bool vfree=false;
		float64_t* vec=lhs->get_feature_vector(i, vlen, vfree);

		for (int32_t j=0; j<m_dimensions; j++)
			m_mus.matrix[Cl*m_dimensions+j] += vec[j];

		lhs->free_feature_vector(vec, i, vfree);
	}

	SG_UNREF(lhs);

	for (int32_t i=0; i<m_k; i++)
	{
		if (weights_set[i]!=0.0)
		{
			for (int32_t j=0; j<m_dimensions; j++)
				m_mus.matrix[i*m_dimensions+j] /= weights_set[i];
		}
	}
}

void CKMeans::set_initial_centers(SGVector<float64_t> weights_set, 
				SGVector<int32_t> ClList, int32_t XSize)
{
	ASSERT(m_mus_initial.matrix);

	/// set rhs to mus_start
	CDenseFeatures<float64_t>* rhs_mus=new CDenseFeatures<float64_t>(0);
	CFeatures* rhs_cache=distance->replace_rhs(rhs_mus);
	rhs_mus->copy_feature_matrix(m_mus_initial);

	SGVector<float64_t> dists=SGVector<float64_t>(m_k*XSize);
	dists.zero();

	for(int32_t idx=0;idx<XSize;idx++)
	{
		for(int32_t m=0;m<m_k;m++)
			dists[m_k*idx+m] = distance->distance(idx,m);
	}

	for (int32_t i=0; i<XSize; i++)
	{
		float64_t mini=dists[i*m_k];
		int32_t Cl = 0, j;

		for (j=1; j<m_k; j++)
		{
			if (dists[i*m_k+j]<mini)
			{
				Cl=j;
				mini=dists[i*m_k+j];
			}
		}
		ClList[i]=Cl;
	}

	/* Compute the sum of all points belonging to a cluster
	 * and count the points */
	CDenseFeatures<float64_t>* lhs=
			(CDenseFeatures<float64_t>*)distance->get_lhs();
	for (int32_t i=0; i<XSize; i++)
	{
		const int32_t Cl = ClList[i];
		weights_set[Cl]+=1.0;

		int32_t vlen=0;
		bool vfree=false;
		float64_t* vec=lhs->get_feature_vector(i, vlen, vfree);

		for (int32_t j=0; j<m_dimensions; j++)
			m_mus.matrix[Cl*m_dimensions+j] += vec[j];

		lhs->free_feature_vector(vec, i, vfree);
	}
	SG_UNREF(lhs);
	distance->replace_rhs(rhs_cache);
	delete rhs_mus;

		/* normalization to get the mean */
	for (int32_t i=0; i<m_k; i++)
	{
		if (weights_set[i]!=0.0)
		{
			for (int32_t j=0; j<m_dimensions; j++)
				m_mus.matrix[i*m_dimensions+j] /= weights_set[i];
		}
	}

}

void CKMeans::compute_cluster_variances()
{
	/* compute the ,,variances'' of the clusters */
	for (int32_t i=0; i<m_k; i++)
	{
		float64_t rmin1=0;
		float64_t rmin2=0;

		bool first_round=true;

		for (int32_t j=0; j<m_k; j++)
		{
			if (j!=i)
			{
				int32_t l;
				float64_t dist = 0;

				for (l=0; l<m_dimensions; l++)
				{
					dist+=CMath::sq(
							m_mus.matrix[i*m_dimensions+l]
									-m_mus.matrix[j*m_dimensions+l]);
				}

				if (first_round)
				{
					rmin1=dist;
					rmin2=dist;
					first_round=false;
				}
				else
				{
					if ((dist<rmin2) && (dist>=rmin1))
						rmin2=dist;

					if (dist<rmin1)
					{
						rmin2=rmin1;
						rmin1=dist;
					}
				}
			}
		}

		m_R.vector[i]=(0.7*CMath::sqrt(rmin1)+0.3*CMath::sqrt(rmin2));
	}
}


bool CKMeans::train_machine(CFeatures* data)
{
	return true;
}

bool CKMeans::load(FILE* srcfile)
{
	SG_SET_LOCALE_C;
	SG_RESET_LOCALE;
	return false;
}

bool CKMeans::save(FILE* dstfile)
{
	SG_SET_LOCALE_C;
	SG_RESET_LOCALE;
	return false;
}

void CKMeans::set_use_kmeanspp(bool kmpp)
{
	m_use_kmeanspp=kmpp;
}

bool CKMeans::get_use_kmeanspp() const
{
	return m_use_kmeanspp;
}

void CKMeans::set_k(int32_t p_k)
{
	REQUIRE(p_k>0, "number of clusters should be > 0");
	this->m_k=p_k;
}

int32_t CKMeans::get_k()
{
	return m_k;
}

void CKMeans::set_max_iter(int32_t iter)
{
	REQUIRE(iter>0, "number of clusters should be > 0");
	m_max_iter=iter;
}

float64_t CKMeans::get_max_iter()
{
	return m_max_iter;
}

SGVector<float64_t> CKMeans::get_radiuses()
{
	return m_R;
}

SGMatrix<float64_t> CKMeans::get_cluster_centers()
{
	if (!m_R.vector)
		return SGMatrix<float64_t>();

	CDenseFeatures<float64_t>* lhs=
		(CDenseFeatures<float64_t>*)distance->get_lhs();
	SGMatrix<float64_t> centers=lhs->get_feature_matrix();
	SG_UNREF(lhs);
	return centers;
}

int32_t CKMeans::get_dimensions()
{
	return m_dimensions;
}

void CKMeans::set_fixed_centers(bool fixed)
{
	m_fixed_centers=fixed;
}

bool CKMeans::get_fixed_centers()
{
	return m_fixed_centers;
}

void CKMeans::store_model_features()
{
	/* set lhs of underlying distance to cluster centers */
	CDenseFeatures<float64_t>* cluster_centers=new CDenseFeatures<float64_t>(
			m_mus);

	/* store cluster centers in lhs of distance variable */
	CFeatures* rhs=distance->get_rhs();
	distance->init(cluster_centers, rhs);
	SG_UNREF(rhs);
}

SGMatrix<float64_t> CKMeans::kmeanspp()
{
	int32_t num=distance->get_num_vec_lhs();
	SGVector<float64_t> dists=SGVector<float64_t>(num);
	SGVector<int32_t> mu_index=SGVector<int32_t>(m_k);
	
	/* 1st center */
	int32_t mu_1=CMath::random((int32_t) 0,num-1);
	mu_index[0]=mu_1;
	
	/* choose a center - do k-1 times */
	int32_t count=0;
	while (++count<m_k)
	{
		float64_t sum=0.0;
		/* for each data point find distance to nearest already chosen center */
		for (int32_t point_idx=0;point_idx<num;point_idx++)
		{
			dists[point_idx]=distance->distance(mu_index[0],point_idx);
			int32_t cent_id=1;

			while (cent_id<count)
			{
				float64_t dist_temp=distance->distance(mu_index[cent_id],point_idx); 
				if (dists[point_idx]>dist_temp)
					dists[point_idx]=dist_temp; 
				cent_id++;
			}

			dists[point_idx]*=dists[point_idx];
			sum+=dists[point_idx];
		}

		/*random choosing - points weighted by square of distance from nearset center*/
		int32_t mu_next=0;
		float64_t chosen=CMath::random(0.0,sum);
		while ((chosen-=dists[mu_next])>0)
			mu_next++;

		mu_index[count]=mu_next;
	}

	CDenseFeatures<float64_t>* lhs=(CDenseFeatures<float64_t>*)distance->get_lhs();
	int32_t dim=lhs->get_num_features();
	SGMatrix<float64_t> mat=SGMatrix<float64_t>(dim,m_k);
	for (int32_t c_m=0;c_m<m_k;c_m++)
	{
		SGVector<float64_t> feature=lhs->get_feature_vector(c_m);
		for (int32_t r_m=0;r_m<dim;r_m++)
			mat(r_m,c_m)=feature[r_m];
	}
	SG_UNREF(lhs);
	return mat;
}

void CKMeans::init()
{
	m_max_iter=10000;
	m_k=3;
	m_dimensions=0;
	m_fixed_centers=false;
	m_use_kmeanspp=false;
	SG_ADD(&m_max_iter, "m_max_iter", "Maximum number of iterations", MS_AVAILABLE);
	SG_ADD(&m_k, "k", "m_k, the number of clusters", MS_AVAILABLE);
	SG_ADD(&m_dimensions, "m_dimensions", "Dimensions of data", MS_NOT_AVAILABLE);
	SG_ADD(&m_R, "m_R", "Cluster radiuses", MS_NOT_AVAILABLE);
}

