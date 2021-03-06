/*
 * This software is distributed under BSD 3-clause license (see LICENSE file).
 *
 * Authors: Jacob Walker, Roman Votyakov, Yuyu Zhang, Sergey Lisitsyn
 */

#ifndef CEVALUATIONRESULT_H_
#define CEVALUATIONRESULT_H_

#include <shogun/lib/config.h>

#include <shogun/base/SGObject.h>

namespace shogun
{

/** Type of evaluation result. Currently this includes Cross Validation and
 * Gradient Evaluation
 */
enum EEvaluationResultType
{
	CROSSVALIDATION_RESULT=0,
	GRADIENTEVALUATION_RESULT=1
};

/** @brief Abstract class that contains the result generated by the
 * MachineEvaluation class.
 */
class CEvaluationResult : public CSGObject
{
public:
	/** default constructor */
	CEvaluationResult() : CSGObject() { }

	virtual ~CEvaluationResult() { }

	/** return what type of result we are.
	 *
	 * abstract base method
	 *
	 * @return result type
	 */
	virtual EEvaluationResultType get_result_type() const=0;

	/** print result */
	virtual void print_result()=0;
};
}
#endif /* CEVALUATIONRESULT_H_ */
