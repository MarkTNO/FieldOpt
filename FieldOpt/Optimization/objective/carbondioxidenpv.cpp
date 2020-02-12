/******************************************************************************
Copyright (C) 2015-2020 Brage S. Kristoffersen <brage_sk@hotmail.com>

This file is part of the FieldOpt project.

FieldOpt is free software: you can redistribute it and/or modify
        it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

FieldOpt is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with FieldOpt.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/
#include "carbondioxidenpv.h"

#include <iostream>
#include <iomanip>
#include "weightedsum.h"
#include <stdlib.h>
#include <cmath>
#include "Model/model.h"
#include "Model/wells/well.h"
#include <Utilities/printer.hpp>

using std::cout;
using std::endl;
using std::fixed;
using std::setprecision;
using std::scientific;

namespace Optimization {
namespace Objective {

carbondioxidenpv::carbondioxidenpv(Settings::Optimizer *settings,
         Simulation::Results::Results *results,
         Model::Model *model) {

  settings_ = settings;
  results_ = results;
  model_ = model;

  components_ = new QList<carbondioxidenpv::Component *>();
  carboncomponents_ = new QList<carbondioxidenpv::Component *>();

  for (int i = 0; i < settings->objective().NPV_sum.size(); ++i) {

      auto *comp = new carbondioxidenpv::Component();
      if (settings->objective().NPV_sum[i].property.compare(0, 4, "EXT-") == 0) {
          comp->is_json_component = true;
          Printer::ext_info("Adding external NPV component.", "Optimization", "carbondioxidenpv");
          comp->property_name = settings->objective().NPV_sum[i].property.substr(4, std::string::npos);
          comp->interval = settings->objective().NPV_sum[i].interval;

      } else {
          Printer::ext_info("NOT adding external NPV component.", "Optimization", "carbondioxidenpv");
          comp->is_json_component = false;
          comp->property_name = settings->objective().NPV_sum.at(i).property;
          cout << "comp->property_name: " << comp->property_name << endl;

          comp->property = results_->GetPropertyKeyFromString(QString::fromStdString(comp->property_name));

      }

      comp->coefficient = settings->objective().NPV_sum.at(i).coefficient;
      if (settings->objective().NPV_sum.at(i).usediscountfactor == true) {
          comp->interval = settings->objective().NPV_sum.at(i).interval;
          comp->discount = settings->objective().NPV_sum.at(i).discount;
          comp->usediscountfactor = settings->objective().NPV_sum.at(i).usediscountfactor;
      } else {
          comp->interval = "None";
          comp->discount = 0;
          comp->usediscountfactor = false;
      }

      components_->append(comp);
  }

  for (int i = 0; i < settings->objective().NPVCarbonComponents.size(); ++i){
      auto *carboncomp = new carbondioxidenpv::Component();
      carboncomp->property_name = settings->objective().NPVCarbonComponents.at(i).property.toStdString();
      carboncomp->property = results_->GetPropertyKeyFromString(QString::fromStdString(carboncomp->property_name));
      carboncomp->is_well_property = settings->objective().NPVCarbonComponents.at(i).is_well_prop;
      if (carboncomp->is_well_property == true){
          carboncomp->well = settings->objective().NPVCarbonComponents.at(i).well;
      }
      carboncomponents_->append(carboncomp);
  }

  well_economy_ = model->wellCostConstructor();

  rho_wi_ = 1000;
  g_ = 9.81;
  reservoir_depth_ = 2500;
  npump_wi_ = 1;
  psuc_ = 1.01325;
  eff_mechanical_ = 0.95;
  max_pow_per_pump_ = 0.75;
  cost_per_pump_ = 1.5;
  enrg_const_wt_ = 0.5;
  unit_cost_wt_ = 2;
  cost_per_turbine_ = 70;
  pow_supply_per_turbine_ = 70;
  co2_em_per_enrg_unit_ = 0.75;
  co2_tax_rate_ = 500;

}

std::vector<double> carbondioxidenpv::calcPM(std::vector<double> WBHP) const {
  std::vector<double> pm;
  for( int i = 0; i < WBHP.size(); i++ ) {
    pm.push_back(WBHP[i] - rho_wi_ * g_ * reservoir_depth_ / pow(10.0, 5.0));
  }
  return pm;
}

double carbondioxidenpv::calcPdis(QList<double> pm_per_report_time) const {
    double pdis = 0;
    for (int i = 0; i < pm_per_report_time.size(); i++) {
        if (pm_per_report_time.at(i) > pdis) {
            pdis = pm_per_report_time.value(i);
        }
    }
    return pdis;
}

double carbondioxidenpv::calcEffHydraulic(double qwi_per_pump) const {
    double eff_hydraulic = (-3.98607*pow(10.0, -12.0)*pow(qwi_per_pump, 4.0)
                           + 2.62704*pow(10.0, -8.0)*pow(qwi_per_pump, 3.0)
                           - 7.18777*pow(10.0, -5.0)*pow(qwi_per_pump, 2.0)
                           + 1.08323*pow(10.0, -1.0)*qwi_per_pump
                           - 9.43801*pow(10.0, -1.0))/100+0.2;
    return eff_hydraulic;

}

double carbondioxidenpv::calcPowPerPump(double pdis, double qwi_per_pump, double eff_hydraulic) const {
        double pow_per_pump = (((pdis-psuc_)*pow(10.0,5.0)*qwi_per_pump/86400)/(eff_hydraulic*eff_mechanical_))/pow(10.0, 6.0);
        return pow_per_pump;
}

QList<double> carbondioxidenpv::calcPowWt(std::vector<double> FWPR) const {
    QList<double> pow_wt;
    for (int i = 0; i < FWPR.size(); i++){
        pow_wt.append(enrg_const_wt_*FWPR[i]/(24*1000));
    }
    return pow_wt;
}

double carbondioxidenpv::calcNTurbine(double pow_demand) const {
   auto Nturbine = ceil(pow_demand/pow_supply_per_turbine_);
   return Nturbine;
}

double carbondioxidenpv::calcCO2EmRate(double pow_generated) const {
   double co2_em_rate = co2_em_per_enrg_unit_*pow_generated*24;
   return co2_em_rate;
}

QList<double> carbondioxidenpv::calcCum(vector<double, allocator<double>> time, QList<double> rate) const {
    QList<double> cum;
    cum.append(0);
    for (int i = 1; i < time.size(); i++){
        cum.append(cum.value(i-1) + (rate.value(i)+rate.value(i-1))/2 * (time.at(i)-time.at(i-1)));
        //cum[i] = cum.value(i-1) + (rate.value(i)+rate.value(i-1))/2 * (time.at(i)-time.at(i-1))*365; // days
}
return cum;
}

double carbondioxidenpv::resolveCarbonDioxideCost(vector<double, allocator<double>> report_times) const {

  std::vector<double> FWIR;
  std::vector<double> FWPR;
  std::vector<double> FWPT;

  std::vector<std::vector<double>> well_bhps;

    for (int j = 0; j < carboncomponents_->size(); ++j) {
        // cout << "property_name: " << carboncomponents_->value(j)->property_name << endl;

        if (carboncomponents_->value(j)->is_well_property == true) {

          for (int w = 0; w < model_->wells()->size(); ++w) {
            // cout << "well: " << model_->wells()->at(w)->name().toStdString() << endl;
            well_bhps.push_back(results_->GetValueVector(Simulation::Results::Results::Property::WellBottomHolePressure,
                                     model_->wells()->at(w)->name()));
          }

        } else if (carboncomponents_->at(j)->property_name == "CumulativeWaterProduction") {
          FWPT = results_->GetValueVector(carboncomponents_->at(j)->property);
          cout << "FWPT.size(): " << FWPT.size() << endl;

        } else if (carboncomponents_->at(j)->property_name == "WaterInjectionRate") {
          FWIR = results_->GetValueVector(carboncomponents_->at(j)->property);
          cout << "FWIR.size(): " << FWIR.size() << endl;

        } else if (carboncomponents_->at(j)->property_name == "WaterProductionRate") {
          FWPR = results_->GetValueVector(carboncomponents_->at(j)->property);
          cout << "FWPR.size(): " << FWPR.size() << endl;
        }

    }

//    QList<QList<double>> pm;
  QList<std::vector<double>> pm;
    for (int i = 0; i < well_bhps.size(); i++){
      pm.append(calcPM(well_bhps[i]));
    }

    QList<QList<double>> pm_transpose;
    for (int j = 0; j< report_times.size();j++){
        QList<double> pm_per_report_time;
        for (int i = 0; i < pm.size(); i++){
            pm_per_report_time.append(pm.at(i).at(j));
        }
        pm_transpose.append(pm_per_report_time);
        pm_per_report_time.clear();
    }

    QList<double> pdis;
    for (int i = 0; i < pm_transpose.size(); i++){
        pdis.append(calcPdis(pm_transpose.at(i)));
    }
    cout << "report_times.size(): " << report_times.size() << endl;

    QList<double> qwi_per_pump;
    QList<double> eff_hydraulic;
    QList<double> temp_qwi_per_pump;
    QList<double> temp_eff_hydraulic;
    double temp_n_pump = npump_wi_;
    bool feasable = false;
    bool temp_feasable;
    while (!feasable) {
        //cout << temp_n_pump << endl;
        temp_qwi_per_pump.clear();
        temp_eff_hydraulic.clear();
        for (int i = 0; i < report_times.size(); i++){
            if (FWIR[i] > 0){
                temp_qwi_per_pump.append(FWIR[i]/temp_n_pump);
                temp_eff_hydraulic.append(calcEffHydraulic(temp_qwi_per_pump.value(i)));
            }
        }
        temp_feasable = true;
        for (int i = 0; i < temp_eff_hydraulic.size(); i++){
            //cout << temp_eff_hydraulic.value(i) << endl;
            if (temp_eff_hydraulic.value(i) <= 0){
                if (temp_feasable){
                    temp_feasable = false;
                }
            }
        }
        feasable = temp_feasable;
        if (!feasable){
            temp_n_pump += 1;

        }
    }
    cout << "Number of pumps" << endl;
    cout << temp_n_pump << endl;
    for (int i = 0; i < report_times.size(); i++){
        qwi_per_pump.append(FWIR[i]/temp_n_pump);
        eff_hydraulic.append(calcEffHydraulic(qwi_per_pump.value(i)));
        if (eff_hydraulic.value(i) <= 0){
            cout << "error@eff_hydraulic" << endl;
        }
    }

    QList<double> pow_per_pump;
    for (int i = 0; i < report_times.size(); i++){
        pow_per_pump.append(calcPowPerPump(pdis.value(i), qwi_per_pump.value(i), eff_hydraulic.value(i)));
        if (pow_per_pump.value(i) > max_pow_per_pump_){
            cout << "error@pow_per_pump" << endl;
        }
    }

    QList<double> pow_inj_system;
    for (int i= 0; i < pow_per_pump.size(); i++){
        pow_inj_system.append(npump_wi_*pow_per_pump.value(i));
    }

    double cost_inj_system = npump_wi_*cost_per_pump_;

    QList<double> pow_wt = calcPowWt(FWPR);

    //for (int i = 0; i < report_times.size(); i++){
    //    pow_wt.append(calcPowWt(FWPR));
    //}

    double cost_op_wt = (FWPT[FWPT.size()-1] - FWPT[0]) * unit_cost_wt_ / pow(10.0, 6.0);

    QList<double> pow_demand;
    QList<double> Nturbine;
    QList<double> pow_generated;
    for (int i = 0; i < report_times.size(); i++){
        pow_demand.append(pow_inj_system.value(i) + pow_wt[i]);
        Nturbine.append(calcNTurbine(pow_demand.value(i)));
        pow_generated.append(Nturbine.at(i)*pow_supply_per_turbine_);
    }

    double max_Nturbine = 0;
    for(int i = 0; i < Nturbine.size(); i++){
        if (max_Nturbine < Nturbine.value(i)){
            max_Nturbine = Nturbine.value(i);
        }
    }

    double cost_turbine = max_Nturbine * cost_per_turbine_;

    QList<double> co2_em_rate;
    for (int i = 0; i < pow_generated.size(); i++){
        co2_em_rate.append(calcCO2EmRate(pow_generated.value(i)));
    }

    QList<double> co2_em_cum;
    co2_em_cum = calcCum(report_times, co2_em_rate);
    for (int i = 0; i < co2_em_cum.size(); i++){
         co2_em_cum[i] = co2_em_cum[i] / pow(10.0, 6.0);
    }

    auto co2_tax = co2_tax_rate_*co2_em_cum.value(co2_em_cum.size()-1);

    cout << "co2_tax:.........." << co2_tax << endl;
    cout << "cost_turbine:....." << cost_turbine << endl;
    cout << "cost_inj_system:.." << cost_inj_system << endl;
    cout << "cost_op_wt:......." << cost_op_wt << endl;
    cout << "..............................................." << endl;

    return co2_tax+cost_turbine+cost_inj_system+cost_op_wt;

}


double carbondioxidenpv::value() const {
  try {
  double value = 0;

  auto report_times = results_->GetValueVector(results_->Time);
  auto NPV_times = new QList<double>;
  auto discount_factor_list = new QList<double>;

  for (int k = 0; k < components_->size(); ++k) {
    if (components_->at(k)->is_json_component == true) {
        continue;
    }
    if (components_->at(k)->interval == "Yearly") {
      double discount_factor;
      int j = 1;
      for (int i = 0; i < report_times.size(); i++) {
        if (i < report_times.size() - 1 &&  (report_times[i+1] - report_times[i]) > 365) {
            std::stringstream ss;
            ss << "Skipping assumed pre-simulation time step " << report_times[i]
               << ". Next time step: " << report_times[i+1] << ". Ignore if this is time 0 in a restart case.";
            Printer::ext_warn(ss.str(), "Optimization", "NPV");
            continue;
        }
        if (std::fmod(report_times.at(i), 365) == 0) {
          discount_factor = 1 / (1 * (pow(1 + components_->at(k)->discount, j - 1)));
          discount_factor_list->append(discount_factor);
          NPV_times->append(i);

          j += 1;
        }
      }
    }else if (components_->at(k)->interval == "Monthly") {
        double discount_factor;
        double discount_rate = components_->at(k)->discount;
        double monthly_discount = components_->at(k)->yearlyToMonthly(discount_rate);
        int j = 1;
        for (int i = 0; i < report_times.size(); i++) {
          if (std::fmod(report_times.at(i), 30) == 0) {
            NPV_times->append(i);
            discount_factor = 1 / (1 * (pow(1 + monthly_discount, j - 1)));
            discount_factor_list->append(discount_factor);
            j += 1;
          }
        }
      }
    }
    for (int i = 0; i < components_->size(); ++i) {
      if (components_->at(i)->is_json_component == true) {
          continue;
      }
      if (components_->at(i)->usediscountfactor == true) {
        for (int j = 1; j < NPV_times->size(); ++j) {
          auto prod_difference = components_->at(i)->resolveValueDiscount(results_, NPV_times->at(j))
              - components_->at(i)->resolveValueDiscount(results_, NPV_times->at(j - 1));
          value += prod_difference * components_->at(i)->coefficient * discount_factor_list->at(i);
        }
      } else if (components_->at(i)->usediscountfactor == false) {
        value += components_->at(i)->resolveValue(results_);
        std::string prop_name = components_->at(i)->property_name;
      }
    }
    if (well_economy_->use_well_cost) {
      for (auto well: well_economy_->wells_pointer) {
        if (well_economy_->separate) {
          value -= well_economy_->costXY * well_economy_->well_xy[well->name().toStdString()];
          value -= well_economy_->costZ * well_economy_->well_z[well->name().toStdString()];
        } else {
          value -= well_economy_->cost * well_economy_->well_lengths[well->name().toStdString()];
        }
      }
    }
    for (int j = 0; j < components_->size(); ++j) {
      if (components_->at(j)->is_json_component == true) {
          if (components_->at(j)->interval == "Single" || components_->at(j)->interval == "None") {
              double extval = components_->at(j)->coefficient
                  * results_->GetJsonResults().GetSingleValue(components_->at(j)->property_name);
              value += extval;

          }
          else {
            Printer::ext_warn("Unable to parse external component.", "Optimization", "NPV");
          }
      }
    }
    QList<double> NPVTimes;
    for (int i = 0; i < NPV_times->size(); i++){
        NPVTimes.append(NPV_times->value(i));
    }
    double carbonCost = resolveCarbonDioxideCost(report_times);
    return value+carbonCost;
  }
  catch (...) {
    Printer::error("Failed to compute carbondioxidenpv. Returning 0.0");
    return 0.0;
  }
}

double carbondioxidenpv::Component::resolveValue(Simulation::Results::Results *results)
{
    if (is_well_property) {
        if (time_step < 0) { // Final time step well property
            return coefficient * results->GetValue(property, well);
        }
        else { // Non-final time step well property
            return coefficient * results->GetValue(property, well, time_step);
        }
    }
    else {
            return coefficient * results->GetValue(property);
    }
}

double carbondioxidenpv::Component::resolveValueDiscount(Simulation::Results::Results *results, double time_step) {
  int time_step_int = (int) time_step;
  return results->GetValue(property, time_step_int);
}

double carbondioxidenpv::Component::yearlyToMonthly(double discount_factor) {
  return pow((1 + discount_factor), 0.083333) - 1;

}

}

}
