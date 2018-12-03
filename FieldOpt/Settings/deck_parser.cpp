/******************************************************************************
   Created by einar on 4/11/18.
   Copyright (C) 2017 Einar J.M. Baumann <einar.baumann@gmail.com>

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
#include "deck_parser.h"
#include <numeric>
#include <boost/lexical_cast.hpp>
#include "Utilities/time.hpp"
#include <opm/parser/eclipse/Units/Units.hpp>
#include <Utilities/verbosity.h>
#include <Utilities/printer.hpp>

namespace Settings {


DeckParser::DeckParser(std::string deck_file) {

    Opm::ParseContext opm_parse_context;
    Opm::Parser opm_parser;

    // Update error policies to be more permissive when it comes
    // to the non-schedule parts of the deck.
    opm_parse_context.update("PARSE_MISSING_DIMS_KEYWORD", Opm::InputError::WARN);
    opm_parse_context.update("PARSE_RANDOM_SLASH", Opm::InputError::WARN);



    auto opm_deck_ = opm_parser.parseFile(deck_file, opm_parse_context);
    Opm::EclipseState state( opm_deck_, opm_parse_context );
    Opm::Schedule opm_schedule( opm_deck_,
                                state.getInputGrid(),
                                state.get3DProperties(),
                                state.runspec().phases(),
                                opm_parse_context);
    num_wells_ = opm_schedule.numWells();
    num_groups_ = opm_schedule.numGroups();
    num_timesteps_ = opm_schedule.getTimeMap().numTimesteps();


    // _________________________________________________________________
    // Initializing time vectors
    time_days_ = std::vector<int>(num_timesteps_);
    time_dates_ = std::vector<std::string>(num_timesteps_);
    time_days_[0] = 0;
    time_dates_[0] = unix_time_to_datestring(opm_schedule.getTimeMap().getStartTime(0));
    for (int i = 1; i < num_timesteps_; ++i) {
        int seconds_from_start = opm_schedule.getTimeMap().getStartTime(i) - opm_schedule.getTimeMap().getStartTime(0);
        int days_from_start = seconds_from_start / 24 / 60 / 60;
        time_days_[i] = days_from_start;
        time_dates_[i] = unix_time_to_datestring(opm_schedule.getTimeMap().getStartTime(i));
    }

    // _________________________________________________________________
    // Loading well data
    for (int i = 0; i < num_wells_; ++i) {
        if (VERB_SET >= 2) {
            Printer::ext_info("Importing well " + opm_schedule.getWells()[i]->name(), "Settings", "DeckParser");
        }
        current_well_name_ = opm_schedule.getWells()[i]->name();
        current_well_first_time_step_ = opm_schedule.getWells()[i]->firstTimeStep();
        well_structs_.append(opmWellToWellStruct(opm_schedule.getWells()[i]));
        if (VERB_SET >= 2) {
            std::stringstream ss;
            ss << "Done importing " << current_well_name_
               << "; " << (well_structs_.last().type == Model::WellType::Injector ? "injector" : "producer")
               << "; start time: " << well_structs_.last().controls.first().time_step << " days"
               << "; nr. connections: " << well_structs_.last().well_blocks.size() << std::endl;
            Printer::ext_info(ss.str(), "Settings", "DeckParser");
        }
    }
}


QList<Model::Well> DeckParser::GetWellData() {
    return well_structs_;
}

Model::Well DeckParser::opmWellToWellStruct(const Opm::Well* opm_well) {
    Model::Well well_struct;
    well_struct.name = QString::fromStdString(current_well_name_);
    well_struct.type = determineWellType(opm_well);
    well_struct.preferred_phase = determinePreferredPhase(opm_well);
    well_struct.well_blocks = opmToWellBlocks(opm_well);
    well_struct.controls = opmToControlEntries(opm_well);
    well_struct.wellbore_radius = determineWellboreRadius(opm_well);
    well_struct.definition_type = Model::WellDefinitionType::WellBlocks;

    return well_struct;
}

Model::WellType DeckParser::determineWellType(const Opm::Well* opm_well) {
    bool is_injector = false;
    bool is_producer = false;

    for (int t = opm_well->firstTimeStep(); t < num_timesteps_; ++t) {
        if (opm_well->getStatus(t) != Opm::WellCommon::StatusEnum::OPEN) {
            continue; // Go to next time step if the well is not yet open
        }
        if (!is_injector && opm_well->isInjector(t)) {
            if (is_producer) {
                Printer::ext_warn("Well " + current_well_name_ + " detected as alternating producer/injector well. "
                    "This is not currently supported. Using first defined state (Producer).", "Settings", "DeckParser");
                is_producer = true;
                is_injector = false;
                break;
            }
            else {
                is_injector = true;
            }
        }
        if (!is_producer && opm_well->isProducer(t)) {
            if (is_injector) {
                Printer::ext_warn("Well " + current_well_name_ + " detected as alternating producer/injector well. "
                    "This is not currently supported. Using first defined state (Injector).", "Settings", "DeckParser");
                is_producer = false;
                is_injector = true;
                break;
            }
            is_producer = true;
        }
    }

    if (!is_injector && !is_producer) {
        if (opm_well->isInjector(opm_well->firstTimeStep())) {
            Printer::ext_warn("Unable to determine well type for " + current_well_name_
            + ". Defaulting to type at first control (injector)", "Settings", "DeckParser");
            is_injector = true;
        }
        else {
            Printer::ext_warn("Unable to determine well type for "
            + current_well_name_ + ". Defaulting to type at first control (producer)", "Settings", "DeckParser");
            is_producer = true;
        }
    }

    if (is_injector && !is_producer) {
        return Model::WellType::Injector;
    }
    else if (!is_injector && is_producer) {
        return Model::WellType::Producer;
    }
    else {
        Printer::ext_warn("Deck parser was unable to determine well type for " + current_well_name_,
            "Settings", "DeckParser");
        return Model::WellType::UNKNOWN_TYPE;
    }
}

Model::PreferredPhase DeckParser::determinePreferredPhase(const Opm::Well* opm_well) {
    Opm::Phase opm_phase = opm_well->getPreferredPhase();
    switch (opm_phase)
    {
        case Opm::Phase::OIL:
            return Model::PreferredPhase::Oil;
        case Opm::Phase::WATER:
            return Model::PreferredPhase::Water;
        case Opm::Phase::GAS:
            return Model::PreferredPhase::Gas;
        default:
            Printer::ext_warn("Deck parser was unable to determine the preferred phase for " + current_well_name_,
                              "Settings", "DeckParser");
            return Model::PreferredPhase::UNKNOWN_PHASE;
    }
}

double DeckParser::determineWellboreRadius(const Opm::Well* opm_well) {

    double radii_sum = 0;
    for (auto comp : opm_well->getCompletions()) {
        radii_sum += (comp.getDiameter() / 2.0);
    }
    double avg_radius = radii_sum / (double)opm_well->getCompletions().size();
    return avg_radius;
}

QList<Model::Well::WellBlock> DeckParser::opmToWellBlocks(const Opm::Well* opm_well) {
    auto well_blocks =  QList<Model::Well::WellBlock>();
    auto headI = opm_well->getHeadI(opm_well->firstTimeStep());
    auto headJ = opm_well->getHeadJ(opm_well->firstTimeStep());
    for (int i = 0; i < opm_well->getCompletions().size(); ++i) {
        Model::Well::Completion comp;
        comp.type = Model::WellCompletionType::Perforation;
        if (opm_well->getCompletions().get(i).getConnectionTransmissibilityFactorAsValueObject().hasValue()) {
            double trans = opm_well->getCompletions().get(i).getConnectionTransmissibilityFactor();
            comp.transmissibility_factor = Opm::unit::convert::to(trans, Opm::Metric::Transmissibility);
        }
        else {
            continue; // Skip block if transmissibility factor not present
        }
        Model::Well::WellBlock wb;
        comp.name = "Transmissibility#" + QString::fromStdString(current_well_name_) + "#" + QString::number(i);
        wb.completion = comp;
        wb.has_completion = true;
        wb.i = opm_well->getCompletions().get(i).getI() + 1;
        wb.j = opm_well->getCompletions().get(i).getJ() + 1;
        wb.k = opm_well->getCompletions().get(i).getK() + 1;
        wb.name = "WellBlock#" + QString::fromStdString(current_well_name_) + "#" + QString::number(i);
        wb.is_variable = false;
        well_blocks.push_back(wb);
    }


    if (well_blocks[0].i <= 0 || abs(well_blocks[0].i) > 10000) { // This tends to happen for some weird reason. Default to same as next block
        std::stringstream ss;
        ss << "(" << well_blocks[0].i << ", " << well_blocks[0].j << ", " << well_blocks[0].k << ")";
        Printer::ext_warn("Invalid i, j or k index detected for the first well block. Deleting it. " + ss.str(),
                          "Settings", "DeckParser");
        well_blocks.erase(well_blocks.begin());
    }

    return well_blocks;
}

QList<Model::Well::ControlEntry> DeckParser::opmToControlEntries(const Opm::Well* opm_well) {
    auto control_entries = QList<Model::Well::ControlEntry>();
    for (int t = current_well_first_time_step_; t < num_timesteps_; ++t) {
//        switch (opm_well->getStatus(t))
//        {
//            case Opm::WellCommon::StatusEnum::OPEN:
//                ce.state = Model::WellState::WellOpen;
//                break;
//            case Opm::WellCommon::StatusEnum::AUTO:
//                ce.state = Model::WellState::WellOpen;
//                break;
//            case Opm::WellCommon::StatusEnum::SHUT:
//                ce.state = Model::WellState::WellShut;
//                break;
//            case Opm::WellCommon::StatusEnum::STOP:
//                ce.state = Model::WellState::WellShut;
//                break;
//            default:
//                // @todo I honestly don't know what could make it go here, but maybe it's that it hasnt changed.
//                continue;
//        }

        if (determineWellControlMode(opm_well, t) != Model::ControlMode::UNKNOWN_CONTROL) {
            Model::Well::ControlEntry ce;
            ce.state = Model::WellState::WellOpen;
            ce.control_mode = determineWellControlMode(opm_well, t);
            ce.rate = determineRate(opm_well, t);
            ce.bhp = determineBhp(opm_well, t);
            if (opm_well->isInjector(t)) {
                ce.injection_type = determineInjectorType(opm_well, t);
            }
            ce.time_step = time_days_[t];

            // Add the new control if it is different from the last one added
            if (control_entries.size() == 0 || control_entries.last().isDifferent(ce)) {
                if (ce.control_mode == Model::ControlMode::RateControl) {
                    ce.name = "Rate#" + QString::fromStdString(current_well_name_) + "#" + QString::number(t);
                }
                else {
                    ce.name = "BHP#" + QString::fromStdString(current_well_name_) + "#" + QString::number(t);
                }
                control_entries.push_back(ce);
            }
        }
    }
    if (control_entries.size() == 0) {
        Printer::ext_warn("Unable to create any valid controls for well " + current_well_name_,
            "Settings", "DeckParser");
    }
    return control_entries;
}

Model::ControlMode DeckParser::determineWellControlMode( const Opm::Well* opm_well, const int timestep ) {
    if (opm_well->isProducer(timestep)) {
        auto opm_wpp = opm_well->getProductionProperties(timestep);
        switch (opm_wpp.controlMode) {
            case Opm::WellProducer::ControlModeEnum::ORAT:
                return Model::ControlMode::RateControl;
            case Opm::WellProducer::ControlModeEnum::RESV:
                return Model::ControlMode::RateControl;
            case Opm::WellProducer::ControlModeEnum::LRAT:
                return Model::ControlMode::RateControl;
            case Opm::WellProducer::ControlModeEnum::BHP:
                return Model::ControlMode::BHPControl;
            default:
                return Model::ControlMode::UNKNOWN_CONTROL;
        }
    }
    else {
        auto opm_wip = opm_well->getInjectionProperties(timestep);
        switch (opm_wip.controlMode) {
            case Opm::WellInjector::ControlModeEnum::RATE:
                return Model::ControlMode::RateControl;
            case Opm::WellInjector::ControlModeEnum::RESV:
                return Model::ControlMode::RateControl;
            case Opm::WellInjector::ControlModeEnum::BHP:
                return Model::ControlMode::BHPControl;
            default:
                return Model::ControlMode::UNKNOWN_CONTROL;
        }
    }
}

double DeckParser::determineRate(const Opm::Well* opm_well, const int timestep) {
    double rate = 0;
    if (opm_well->isInjector(timestep)) {
        auto ips = opm_well->getInjectionProperties(timestep);
        if (ips.surfaceInjectionRate > ips.reservoirInjectionRate) {
            rate = Opm::unit::convert::to(ips.surfaceInjectionRate, Opm::Metric::LiquidSurfaceVolume);
        }
        else {
            rate = Opm::unit::convert::to(ips.reservoirInjectionRate, Opm::Metric::ReservoirVolume);
        }
    }
    else {
        auto pps = opm_well->getProductionProperties(timestep);
        if (pps.OilRate > pps.LiquidRate && pps.OilRate > pps.ResVRate) {
            rate = Opm::unit::convert::to(pps.OilRate, Opm::Metric::LiquidSurfaceVolume);
        }
        else if (pps.LiquidRate > pps.OilRate && pps.LiquidRate > pps.ResVRate) {
            rate = Opm::unit::convert::to(pps.LiquidRate, Opm::Metric::LiquidSurfaceVolume);
        }
        else {
            rate = Opm::unit::convert::to(pps.ResVRate, Opm::Metric::ReservoirVolume);
        }
    }
    return rate;
}

double DeckParser::determineBhp(const Opm::Well* opm_well, const int timestep) {
    double bhp = 0;
    if (opm_well->isInjector(timestep)) {
        auto ips = opm_well->getInjectionProperties(timestep);
        if (ips.BHPLimit > ips.THPLimit) {
            bhp = Opm::unit::convert::to(ips.THPLimit, Opm::Metric::Pressure);
        }
        else {
            bhp = Opm::unit::convert::to(ips.BHPLimit, Opm::Metric::Pressure);
        }
    }
    else {
        auto pps = opm_well->getProductionProperties(timestep);
        if (pps.BHPLimit > pps.THPLimit) {
            bhp = Opm::unit::convert::to(pps.THPLimit, Opm::Metric::Pressure);
        }
        else {
            bhp = Opm::unit::convert::to(pps.BHPLimit, Opm::Metric::Pressure);
        }
    }
    return bhp;
}

Model::InjectionType DeckParser::determineInjectorType(const Opm::Well* opm_well, const int timestep) {
    auto ips = opm_well->getInjectionProperties(timestep);
    if (ips.injectorType == Opm::WellInjector::TypeEnum::WATER)
        return Model::InjectionType::WaterInjection;
    else if (ips.injectorType == Opm::WellInjector::TypeEnum::GAS)
        return Model::InjectionType::GasInjection;
    else {
        Printer::ext_warn("Unable to detect injection type for " + current_well_name_,
            "Settings", "DeckParser");
        return Model::InjectionType::WaterInjection;
    }
}

const std::vector<int> DeckParser::GetTimeDays() {
    return time_days_;
}

const std::vector<std::string> DeckParser::GetTimeDates() {
    return time_dates_;
}

//DeckParser::~DeckParser() {
//    delete(opm_schedule_.get());
//}
}
