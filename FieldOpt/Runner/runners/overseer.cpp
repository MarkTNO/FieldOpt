#include "overseer.h"

namespace Runner {
    namespace MPI {
        Overseer::Overseer(MPIRunner *runner) {
            runner_ = runner;
            runner_->BroadcastModel();

            for (int i = 1; i < runner->world_.size(); ++i) {
                workers_.insert(i, new WorkerStatus(i));
            }
            runner_->printMessage("Initialized overseer.");
        }

        void Overseer::AssignCase(Optimization::Case *c) {
            if (NumberOfFreeWorkers() == 0) throw std::runtime_error("Cannot assign Case. No free workers found.");
            auto worker = getFreeWorker();
            runner_->SendCase(c, worker->rank, MPIRunner::MsgTag::CASE_UNEVAL);
            worker->start();
            runner_->printMessage("Assigned case to worker " + std::to_string(worker->rank), 2);
        }

        Optimization::Case *Overseer::RecvEvaluatedCase() {
            int worker_rank;
            auto evaluated_case = runner_->RecvCase(worker_rank, MPIRunner::MsgTag::CASE_EVAL);
            workers_[worker_rank]->stop();
            runner_->printMessage("Received case from worker " + std::to_string(worker_rank));
            return evaluated_case;
        }

        Overseer::WorkerStatus * Overseer::getFreeWorker() {
            if (NumberOfFreeWorkers() == 0) throw std::runtime_error("No free workers in network.");
            for (int i = 1; i < runner_->world_.size(); ++i) {
                if (workers_[i]->working == false) return workers_[i];
            }
        }

        int Overseer::NumberOfFreeWorkers() {
            int f = 0;
            for (int i = 1; i < runner_->world_.size(); ++i) {
                if (workers_[i]->working == false)
                    f++;
            }
            return f;
        }

        Overseer::WorkerStatus * Overseer::GetLongestRunningWorker() {
            if (NumberOfFreeWorkers() == runner_->world_.size()-1) return nullptr;
            int longest_running_time = -1;
            int longest_running_worker;
            for (int i = 1; i < runner_->world_.size(); ++i) {
                if (workers_[i]->working_seconds() > longest_running_time) {
                    longest_running_time = workers_[i]->working_seconds();
                    longest_running_worker = workers_[i]->rank;
                }
            }
            return workers_[longest_running_worker];
        }

        void Overseer::TerminateWorkers() {
            for (int i = 1; i < runner_->world_.size(); ++i) {
                runner_->SendCase(new Optimization::Case(), i, MPIRunner::MsgTag::TERMINATE);
            }
        }

        int Overseer::NumberOfBusyWorkers() {
            return workers_.count() - NumberOfFreeWorkers();
        }
    }
}