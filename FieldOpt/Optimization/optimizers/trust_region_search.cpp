#include "trust_region_search.h"

namespace Optimization {
    namespace Optimizers {

        TrustRegionSearch::TrustRegionSearch(::Settings::Optimizer *settings, Case *base_case,
                                     Model::Properties::VariablePropertyContainer *variables,
                                     Reservoir::Grid::Grid *grid)
                : Optimizer(settings, base_case, variables, grid)
        {
            radius_ = settings->parameters().initial_step_length;
            minimum_radius_ = settings->parameters().minimum_step_length;
        }

        void TrustRegionSearch::step()
        {
            // applyNew.. sets best case so far to new best.
            // let's just go with the flow for now
            PolyModel::CaseFromPoint(tentative_best_case_->GetRealVarVector(), tentative_best_case_);

            applyNewTentativeBestCase();
            polymodel_.addCenterPoint(GetTentativeBestCase());
        }

        void TrustRegionSearch::scaleRadius(double k)
        {
            radius_ = k*radius_;
        }

        void TrustRegionSearch::perturb()
        {
            QList<Case *> perturbations = QList<Case *>();
            for (QUuid id : tentative_best_case_->integer_variables().keys())
                perturbations.append(tentative_best_case_->Perturb(id, Case::SIGN::PLUSMINUS, radius_));
            for (QUuid id : tentative_best_case_->real_variables().keys())
                perturbations.append(tentative_best_case_->Perturb(id, Case::SIGN::PLUSMINUS, radius_));
            for (Case *c : perturbations) {
                constraint_handler_->SnapCaseToConstraints(c);
            }
            case_handler_->AddNewCases(perturbations);
        }

        Optimizer::TerminationCondition TrustRegionSearch::IsFinished()
        {
            if (case_handler_->EvaluatedCases().size() >= max_evaluations_)
                return MAX_EVALS_REACHED;
            else if (radius_ < minimum_radius_)
                return MINIMUM_STEP_LENGTH_REACHED;
            else return NOT_FINISHED; // The value of not finished is 0, which evaluates to false.
        }

        void TrustRegionSearch::iterate() {
            std::cout << "this is iteration number " << iteration_ << std::endl;
            /* Everytime we update model we must first have a PolyModel object,
             * then we must complete the set of points in the model, then the
             * objective values of all cases must be calculated, and lastly we
             * can return the PolyModel coefficients. This functions checks these
             * steps, starting with the first one. We start at the first incomplete
             * step and perform all the steps that come after it.
             */

            // At the first iteration we initialze the PolyModel with base_case
            if (iteration_ == 0) {
                initializeModel();
            }

            /* Either the PolyModel is ready to give us a derivative, or
             * we need to update points or get cases evaluated
             */
            else if(!polymodel_.isModelReady()) {
                completeModel();
            }

            else {
                std::cout << "Model should be ready, we make some opt step " << std::endl;
                /* TODO Here we can use current_model to do an optimization step, which
                * should include finding a new (improved) point/case. Next use this
                 * point as the new center_point in model and maybe reduce radius, then
                * redo the updateModel thing
                */
                polymodel_.calculate_model_coeffs();
                // Just a dummy thing to stop optimization
                step();
                std:: cout << "end of opt step part" << std::endl;
            }
            case_handler_->ClearRecentlyEvaluatedCases();
        }

        void TrustRegionSearch::initializeModel() {
            // Create polymodel with radius and center and complete set of points.
            polymodel_ = PolyModel(GetTentativeBestCase(), radius_);
            completeModel();
        }

        void TrustRegionSearch::completeModel() {
            polymodel_.complete_points();
            // Add cases to case_handler and clear CasesNotEval queue
            case_handler_->AddNewCases(polymodel_.get_cases_not_eval());
            polymodel_.ClearCasesNotEval();
            polymodel_.set_evaluations_complete();
        }

        QString TrustRegionSearch::GetStatusStringHeader() const
        {
            return QString("%1,%2,%3,%4,%5,%6,%7")
                    .arg("Iteration")
                    .arg("EvaluatedCases")
                    .arg("QueuedCases")
                    .arg("RecentlyEvaluatedCases")
                    .arg("TentativeBestCaseID")
                    .arg("TentativeBestCaseOFValue")
                    .arg("StepLength");
        }

        QString TrustRegionSearch::GetStatusString() const
        {
            return QString("%1,%2,%3,%4,%5,%6,%7")
                    .arg(iteration_)
                    .arg(nr_evaluated_cases())
                    .arg(nr_queued_cases())
                    .arg(nr_recently_evaluated_cases())
                    .arg(tentative_best_case_->id().toString())
                    .arg(tentative_best_case_->objective_function_value())
                    .arg(radius_);
        }

    }}