// Gaga: lightweight simple genetic algorithm library
// Copyright (c) Jean Disset 2016, All rights reserved.

// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 3.0 of the License, or (at your option) any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public
// License along with this library.

#ifndef GAMULTI_HPP
#define GAMULTI_HPP

/****************************************
 *       TO ENABLE PARALLELISATION
 * *************************************/
// before including this file,
// #define OMP if you want OpenMP parallelisation
// #define CLUSTER if you want MPI parallelisation
// #define CLUSTER if you want MPI parallelisation
#ifdef CLUSTER
#include <mpi.h>
#include <cstring>
#endif
#ifdef OMP
#include <omp.h>
#endif

#include <experimental/filesystem>
namespace fs = std::experimental::filesystem::v1;

#if defined(_WIN32)
#define NO_FANCY_OUTPUT
#endif

#include <assert.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <limits>  // std::numeric_limits<double>::infinity
#include "include/json.hpp"

#if defined (NO_COLORS_OUTPUT)
#define PURPLE ""
#define PURPLEBOLD ""
#define BLUE ""
#define BLUEBOLD ""
#define GREY ""
#define GREYBOLD ""
#define YELLOW ""
#define YELLOWBOLD ""
#define RED ""
#define REDBOLD ""
#define CYAN ""
#define CYANBOLD ""
#define GREEN ""
#define GREENBOLD ""
#define NORMAL ""
#else
#define PURPLE "\033[35m"
#define PURPLEBOLD "\033[1;35m"

#ifdef _WIN32
#define BLUE "\033[92m"
#define BLUEBOLD "\033[1;92m"
#else
#define BLUE "\033[34m"
#define BLUEBOLD "\033[1;34m"
#endif
#define GREY "\033[30m"
#define GREYBOLD "\033[1;30m"
#define YELLOW "\033[33m"
#define YELLOWBOLD "\033[1;33m"
#define RED "\033[31m"
#define REDBOLD "\033[1;31m"
#define CYAN "\033[36m"
#define CYANBOLD "\033[1;36m"
#define GREEN "\033[32m"
#define GREENBOLD "\033[1;32m"
#define NORMAL "\033[0m"
#endif

namespace GAGA {

using std::vector;
using std::string;
using std::unordered_set;
using std::map;
using std::unordered_map;
using std::cout;
using std::cerr;
using std::endl;
using fpType = std::vector<std::vector<double>>;  // footprints for novelty
using json = nlohmann::json;
using std::chrono::high_resolution_clock;
using std::chrono::milliseconds;
using std::chrono::system_clock;

/******************************************************************************************
 *                                 GAGA LIBRARY
 *****************************************************************************************/
// This file contains :
// 1 - the Individual class template : an individual's generic representation, with its
// dna, fitnesses and behavior footprints (for novelty)
// 2 - the main GA class template

// About parallelisation :
// before including this file,
// #define OMP if you want OpenMP parallelisation
// #define CLUSTER if you want MPI parallelisation

/*****************************************************************************
 *                         INDIVIDUAL CLASS
 * **************************************************************************/
// A valid DNA class must have (see examples folder):
// DNA mutate()
// DNA crossover(DNA& other)
// static DNA random(int argc, char** argv)
// json& constructor
// void reset()
// json toJson()

template <typename DNA> struct Individual {
    DNA dna;
    map<string, double> fitnesses;  // map {"fitnessCriterName" -> "fitnessValue"}
    fpType footprint;               // individual's footprint for novelty computation
    string infos;                   // custom infos, description, whatever...
    bool evaluated = false;
    bool wasAlreadyEvaluated = false;
    double evalTime = 0.0;

    // NSGA-II related stuff
    std::vector<Individual*>    sp;
    int                         np;
    int                         paretoRank;
    double                      crowdingDistance;

    Individual() {}
    explicit Individual(const DNA &d) : dna(d) {}

    explicit Individual(const json &o) {
        assert(o.count("dna"));
        dna = DNA(o.at("dna").dump());
        if (o.count("footprint")) footprint = o.at("footprint").get<fpType>();
        if (o.count("fitnesses")) fitnesses = o.at("fitnesses").get<decltype(fitnesses)>();
        if (o.count("infos")) infos = o.at("infos");
        if (o.count("evaluated")) evaluated = o.at("evaluated");
        if (o.count("alreadyEval")) wasAlreadyEvaluated = o.at("alreadyEval");
        if (o.count("evalTime")) evalTime = o.at("evalTime");
    }

    // Exports individual to json
    json toJSON() const {
        json o;
        o["dna"] = dna.serialize();
        o["fitnesses"] = fitnesses;
        o["footprint"] = footprint;
        o["infos"] = infos;
        o["evaluated"] = evaluated;
        o["alreadyEval"] = wasAlreadyEvaluated;
        o["evalTime"] = evalTime;
        return o;
    }

    // Exports a vector of individual to json
    static json popToJSON(const vector<Individual<DNA>> &p) {
        json o;
        json popArray;
        for (auto &i : p) popArray.push_back(i.toJSON());
        o["population"] = popArray;
        return o;
    }

    // Loads a vector of individual from json
    static vector<Individual<DNA>> loadPopFromJSON(const json &o) {
        assert(o.count("population"));
        vector<Individual<DNA>> res;
        json popArray = o.at("population");
        for (auto &ind : popArray) res.push_back(Individual<DNA>(ind));
        return res;
    }
};

/*********************************************************************************
 *                                 GA CLASS
 ********************************************************************************/
// DNA requirements : see Individual class;
//
// Evaluaor class requirements (see examples folder):
// constructor(int argc, char** argv)
// void operator()(const Individual<DNA>& ind)
// const string name
//
// TYPICAL USAGE :
//
// GA<DNAType, EvalType> ga;
// ga.setPopSize(400);
// return ga.start();

enum class SelectionMethod { paretoTournament, randomObjTournament, nsga2Tournament };
template <typename DNA> class GA {
 protected:
    /*********************************************************************************
     *                            MAIN GA SETTINGS
     ********************************************************************************/
    bool novelty = false;              // enable novelty
    unsigned int verbosity = 2;        // 0 = silent; 1 = ions stats;
                                       // 2 = individuals stats; 3 = everything
    size_t popSize = 500;              // nb of individuals in the population
    size_t nbElites = 1;               // nb of elites to keep accross generations
    size_t nbSavedElites = 1;          // nb of elites to save
    size_t tournamentSize = 3;         // nb of competitors in tournament
    double minNoveltyForArchive = 1;   // min novelty for being added to the general archive
    size_t KNN = 15;                   // size of the neighbourhood for novelty
    bool savePopEnabled = true;        // save the whole population
    bool saveArchiveEnabled = true;    // save the novelty archive
    unsigned int savePopInterval = 1;  // interval between 2 whole population saves
    unsigned int saveGenInterval = 1;  // interval between 2 elites/pareto saves
    string folder = "../evos/";        // where to save the results
    string evaluatorName;              // name of the given evaluator func
    double crossoverProba = 0.2;       // crossover probability
    double mutationProba = 0.5;        // mutation probablility
    bool evaluateAllIndividuals = false;  // force evaluation of every individual
    bool doSaveParetoFront = false;       // save the pareto front
    bool doSaveGenStats = true;           // save generations stats to csv file
    bool doSaveIndStats = false;          // save individuals stats to csv file
    SelectionMethod selecMethod = SelectionMethod::paretoTournament;

    /********************************************************************************
     *                                 SETTERS
     ********************************************************************************/
 public:
    using DNA_t = DNA;
    void enableNovelty() { novelty = true; }
    void disableNovelty() { novelty = false; }
    void enablePopulationSave() { savePopEnabled = true; }
    void disablePopulationSave() { savePopEnabled = false; }
    void enableArchiveSave() { saveArchiveEnabled = true; }
    void disableArchiveSave() { saveArchiveEnabled = false; }
    void setVerbosity(unsigned int lvl) { verbosity = lvl <= 3 ? (lvl >= 0 ? lvl : 0) : 3; }
    void setPopSize(size_t s) { popSize = s; }
    void setNbElites(size_t n) { nbElites = n; }
    void setNbSavedElites(size_t n) { nbSavedElites = n; }
    void setTournamentSize(size_t n) { tournamentSize = n; }
    void setKNN(size_t n) { KNN = n; }
    void setPopSaveInterval(unsigned int n) { savePopInterval = n; }
    void setGenSaveInterval(unsigned int n) { saveGenInterval = n; }
    void setSaveFolder(string s) { folder = s; }
    void setCrossoverProba(double p) {
        crossoverProba = p <= 1.0 ? (p >= 0.0 ? p : 0.0) : 1.0;
    }
    void setMutationProba(double p) {
        mutationProba = p <= 1.0 ? (p >= 0.0 ? p : 0.0) : 1.0;
    }
    void setEvaluator(std::function<void(Individual<DNA> &)> e,
                      std::string ename = "anonymousEvaluator") {
        evaluator = e;
        evaluatorName = ename;
    }
    void setNewGenerationFunction(std::function<void(void)> f) {
        newGenerationFunction = f;
    }
    void setMinNoveltyForArchive(double m) { minNoveltyForArchive = m; }
    void setIsBetterMethod(std::function<bool(double, double)> f) { isBetter = f; }
    void setSelectionMethod(const SelectionMethod &sm) {
        selecMethod = sm;
        switch (sm) {
            case SelectionMethod::paretoTournament:
                selection = [this]() { return paretoTournament(); };
                break;

            case SelectionMethod::nsga2Tournament:
                // NOTE(charly): Should not be needed
                break;

            case SelectionMethod::randomObjTournament:
            default:
                selection = [this]() { return randomObjTournament(); };
                break;
        }
    }

    void setEvaluateAllIndividuals(bool m) { evaluateAllIndividuals = m; }
    void setSaveParetoFront(bool m) { doSaveParetoFront = m; }
    void setSaveGenStats(bool m) { doSaveGenStats = m; }
    void setSaveIndStats(bool m) { doSaveIndStats = m; }
    vector<Individual<DNA>> population;
    vector<Individual<DNA>> lastGen;

    ////////////////////////////////////////////////////////////////////////////////////

 protected:
    vector<Individual<DNA>>
        archive;  // when novelty is enabled, we store the novel individuals there
    size_t currentGeneration = 0;
    bool customInit = false;
    // openmp/mpi stuff
    int procId = 0;
    int nbProcs = 1;
    int argc = 1;
    char **argv = nullptr;

    std::vector<std::map<std::string, std::map<std::string, double>>> genStats;

    std::random_device rd;
    std::default_random_engine globalRand = std::default_random_engine(rd());

    std::function<void(Individual<DNA> &)> evaluator;
    std::function<Individual<DNA> *()> selection;
    std::function<void(void)> newGenerationFunction = []() {};
    std::function<bool(double, double)> isBetter = [](double a, double b) { return a > b; };

 public:
    /*********************************************************************************
     *                              CONSTRUCTOR
     ********************************************************************************/
    GA(int ac, char **av) : argc(ac), argv(av) {
        setSelectionMethod(selecMethod);
#ifdef CLUSTER
        MPI_Init(&argc, &argv);
        MPI_Comm_size(MPI_COMM_WORLD, &nbProcs);
        MPI_Comm_rank(MPI_COMM_WORLD, &procId);
        if (procId == 0) {
            if (verbosity >= 3) {
                std::cout << "   -------------------" << endl;
                std::cout << CYAN << " MPI STARTED WITH " << NORMAL << nbProcs << CYAN
                          << " PROCS " << NORMAL << endl;
                std::cout << "   -------------------" << endl;
                std::cout << "Initialising population in master process" << endl;
            }
        }
#endif
    }

    /*********************************************************************************
     *                          START THE BOUZIN
     ********************************************************************************/
    void setPopulation(const vector<Individual<DNA>> &p) {
        if (procId == 0) {
            population = p;
            if (population.size() != popSize)
                throw std::invalid_argument("Population doesn't match the popSize param");
            popSize = population.size();
        }
    }

    void initPopulation(const std::function<DNA()> &f) {
        if (procId == 0) {
            population.reserve(popSize);
            for (size_t i = 0; i < popSize; ++i) {
                population.push_back(Individual<DNA>(f()));
                population[population.size() - 1].evaluated = false;
            }
        }
    }

    // "Vroum vroum"
    void step(int nbGeneration = 1) {
        if (!evaluator) throw std::invalid_argument("No evaluator specified");

        if (currentGeneration == 0 && procId == 0)
        {
            createFolder(folder);
            if (verbosity >= 1) printStart();
        }
        
        if (selecMethod == SelectionMethod::nsga2Tournament)
        {
            nsga2Step(nbGeneration);
        }
        else
        {
            for (int nbg = 0; nbg < nbGeneration; ++nbg) {

                auto tg0 = high_resolution_clock::now();
                evaluatePopulation(population);

                if (procId == 0) {
                    if (population.size() != popSize)
                        throw std::invalid_argument("Population doesn't match the popSize param");
                    if (novelty) updateNovelty();
                    auto tg1 = high_resolution_clock::now();
                    double totalTime = std::chrono::duration<double>(tg1 - tg0).count();
                    updateStats(totalTime);
                    auto tnp0 = high_resolution_clock::now();
                    if (currentGeneration % savePopInterval == 0) {
                        if (savePopEnabled) savePop();
                        if (novelty && saveArchiveEnabled) saveArchive();
                    }
                    if (verbosity >= 1) printGenStats(currentGeneration);
                    if (currentGeneration % saveGenInterval == 0) {
                        if (doSaveParetoFront) {
                            saveParetoFront();
                        } else {
                            saveBests(nbSavedElites);
                        }
                    }
                    if (doSaveGenStats) saveGenStats();
                    if (doSaveIndStats) saveIndStats();

                    prepareNextPop();
                    auto tnp1 = high_resolution_clock::now();
                    double tnp = std::chrono::duration<double>(tnp1 - tnp0).count();
                    if (verbosity >= 2) {
                        std::cout << "Time for save + next pop = " << tnp << " s." << std::endl;
                    }
                }
                ++currentGeneration;
            }
        }
    }

    void nsga2Step(int nbGenerations)
    {
        // Evaluate parent population only the first time
        if (currentGeneration == 0)
        {
            evaluatePopulation(population);
        }

        std::uniform_real_distribution<> d(0.0, 1.0);
        auto rng = std::bind(d, globalRand);

        for (int gen = 0; gen < nbGenerations; ++gen)
        {
            auto tg0 = high_resolution_clock::now();
            // Generate new pop Qt
            std::vector<size_t> a(popSize), b(popSize);

            for (size_t i = 0; i < popSize; ++i) a[i] = b[i] = i;
            std::shuffle(a.begin(), a.end(), globalRand);
            std::shuffle(b.begin(), b.end(), globalRand);

            std::vector<Individual<DNA>> child_pop;
            std::vector<Individual<DNA>> mixed_pop;

            // FIXME(charly): Validate the new population creation
            for (size_t i = 0; i < popSize; i += 4)
            {
                Individual<DNA>* p00 = nsga2Tournament(&population[a[i+0]], &population[a[i+1]]);
                Individual<DNA>* p01 = nsga2Tournament(&population[a[i+2]], &population[a[i+3]]);

                if (rng() < crossoverProba)
                {
                    Individual<DNA> c0, c1;
                    c0.dna = p00->dna.crossover(p01->dna);
                    c1.dna = p01->dna.crossover(p00->dna);

                    child_pop.push_back(c0);
                    child_pop.push_back(c1);
                }
                else
                {
                    child_pop.push_back(*p00);
                    child_pop.push_back(*p01);
                }

                Individual<DNA>* p10 = nsga2Tournament(&population[b[i+0]], &population[b[i+1]]);
                Individual<DNA>* p11 = nsga2Tournament(&population[b[i+2]], &population[b[i+3]]);

                if (rng() < crossoverProba)
                {
                    Individual<DNA> c0, c1;
                    c0.dna = p10->dna.crossover(p11->dna);
                    c1.dna = p11->dna.crossover(p10->dna);

                    child_pop.push_back(c0);
                    child_pop.push_back(c1);
                }
                else
                {
                    child_pop.push_back(*p10); 
                    child_pop.push_back(*p11);
                }
            }

            assert(child_pop.size() == population.size());

            // Mutate pop Qt 
            for (auto& indiv : child_pop)
            {
                if (rng() < mutationProba)
                {
                    indiv.dna.mutate();
                }
            }

            // Evaluate Qt
            evaluatePopulation(child_pop);

            // Merge Pt and Qt -> Rt
            mixed_pop.clear();
            mixed_pop.insert(mixed_pop.end(), population.begin(), population.end());
            mixed_pop.insert(mixed_pop.end(), child_pop.begin(), child_pop.end());

            // Sort Rt (nsga2)
            nsga2SortPopulation(mixed_pop);

            // Generate P(t+1)
            std::vector<Individual<DNA>> new_population;

            int front = 0;
            while (new_population.size() + paretoFronts[front].size() < population.size())
            {
                for (auto indiv : paretoFronts[front])
                {
                    new_population.push_back(*indiv);
                }

                ++front;
            }
            
            // Take best individuals of the last front 
            int indiv_idx = 0;
            while (new_population.size() < population.size())
            {
                new_population.push_back(*paretoFronts[front][indiv_idx]);
            }

            lastGen = population;
            population = new_population;

            if (procId == 0)
            {
                if (novelty) updateNovelty();
                auto tg1 = high_resolution_clock::now();
                double totalTime = std::chrono::duration<double>(tg1 - tg0).count();
                updateStats(totalTime);
                if (verbosity >= 1) printGenStats(currentGeneration);
            }

            ++currentGeneration;
        }
    }

    void finish() {
#ifdef CLUSTER
        MPI_Finalize();
#endif
    }

    void evaluatePopulation(std::vector<Individual<DNA>>& pop)
    {
        newGenerationFunction();

#ifdef CLUSTER
        MPI_distributePopulation(pop);
#endif
#ifdef OMP
#pragma omp parallel for schedule(dynamic, 1)
#endif
        for (size_t i = 0; i < pop.size(); ++i) {
            if (evaluateAllIndividuals || !pop[i].evaluated) {
                auto t0 = high_resolution_clock::now();
                pop[i].dna.reset();
                evaluator(pop[i]);
                auto t1 = high_resolution_clock::now();
                pop[i].evaluated = true;
                double indTime = std::chrono::duration<double>(t1 - t0).count();
                pop[i].evalTime = indTime;
                pop[i].wasAlreadyEvaluated = false;
            } else {
                pop[i].evalTime = 0.0;
                pop[i].wasAlreadyEvaluated = true;
            }
            if (verbosity >= 2) printIndividualStats(pop[i]);
        }
#ifdef CLUSTER
        MPI_receivePopulation(pop);
#endif

    }

    // MPI specifics
#ifdef CLUSTER
    void MPI_distributePopulation(std::vector<Individual<DNA>>& pop) {
        if (procId == 0) {
            // if we're in the master process, we send b(i)atches to the others.
            // master will have the remaining
            size_t batchSize = pop.size() / nbProcs;
            for (size_t dest = 1; dest < (size_t)nbProcs; ++dest) {
                vector<Individual<DNA>> batch;
                for (size_t ind = 0; ind < batchSize; ++ind) {
                    batch.push_back(pop.back());
                    pop.pop_back();
                }
                string batchStr = Individual<DNA>::popToJSON(batch).dump();
                std::vector<char> tmp(batchStr.begin(), batchStr.end());
                tmp.push_back('\0');
                MPI_Send(tmp.data(), tmp.size(), MPI_BYTE, dest, 0, MPI_COMM_WORLD);
            }
        } else {
            // we're in a slave process, we welcome our local population !
            int strLength;
            MPI_Status status;
            MPI_Probe(0, 0, MPI_COMM_WORLD, &status);  // we want to know its size
            MPI_Get_count(&status, MPI_CHAR, &strLength);
            char *popChar = new char[strLength + 1];
            MPI_Recv(popChar, strLength, MPI_BYTE, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            // and we dejsonize !
            auto o = json::parse(popChar);
            pop = Individual<DNA>::loadPopFromJSON(o);  // welcome bros!
            if (verbosity >= 3) {
                std::ostringstream buf;
                buf << endl
                    << "Proc " << PURPLE << procId << NORMAL << " : reception of "
                    << pop.size() << " new individuals !" << endl;
                cout << buf.str();
            }
        }
    }

    void MPI_receivePopulation(std::vector<Individual<DNA>>& pop) {
        if (procId != 0) {  // if slave process we send our population to our mighty leader
            string batchStr = Individual<DNA>::popToJSON(pop).dump();
            std::vector<char> tmp(batchStr.begin(), batchStr.end());
            tmp.push_back('\0');
            MPI_Send(tmp.data(), tmp.size(), MPI_BYTE, 0, 0, MPI_COMM_WORLD);
        } else {
            // master process receives all other batches
            for (size_t source = 1; source < (size_t)nbProcs; ++source) {
                int strLength;
                MPI_Status status;
                MPI_Probe(source, 0, MPI_COMM_WORLD, &status);  // determining batch size
                MPI_Get_count(&status, MPI_CHAR, &strLength);
                char *popChar = new char[strLength + 1];
                MPI_Recv(popChar, strLength + 1, MPI_BYTE, source, 0, MPI_COMM_WORLD,
                        MPI_STATUS_IGNORE);
                // and we dejsonize!
                auto o = json::parse(popChar);
                vector<Individual<DNA>> batch = Individual<DNA>::loadPopFromJSON(o);
                pop.insert(pop.end(), batch.begin(), batch.end());
                delete popChar;
                if (verbosity >= 3) {
                    cout << endl
                        << "Proc " << procId << " : reception of " << batch.size()
                        << " treated individuals from proc " << source << endl;
                }
            }
        }
    }
#endif
    /*********************************************************************************
     *                            NEXT POP GETTING READY
     ********************************************************************************/
    // Là où qu'on fait les bébés.
    void prepareNextPop() {
        assert(tournamentSize > 0);
        assert(population.size() == popSize);
        vector<Individual<DNA>> nextGen;
        nextGen.reserve(popSize);
        std::uniform_real_distribution<double> d(0.0, 1.0);

        // Save this generation
        lastGen = population;

        // elitism
        auto elites = getElites(nbElites);
        for (auto &e : elites)
            for (auto &i : e.second) nextGen.push_back(i);

        if (verbosity >= 3) cerr << "preparing rest of the population" << endl;
        while (nextGen.size() < popSize) {
            // selection + crossover
            Individual<DNA> *p0 = selection();
            Individual<DNA> offspring;
            if (d(globalRand) < crossoverProba) {
                if (verbosity >= 3) cerr << "crossover" << endl;
                Individual<DNA> *p1 = selection();
                offspring = Individual<DNA>(p0->dna.crossover(p1->dna));
                offspring.evaluated = false;
                if (verbosity >= 3) cerr << "crossover ok" << endl;
            } else {
                if (verbosity >= 3) cerr << "no crossover" << endl;
                offspring = *p0;
            }
            // mutation
            if (d(globalRand) < mutationProba) {
                if (verbosity >= 3) cerr << "mutation" << endl;
                offspring.dna.mutate();
                offspring.evaluated = false;
            }
            if (verbosity >= 3) cerr << "pushing" << endl;
            nextGen.push_back(offspring);
        }
        if (verbosity >= 3) cerr << "done" << endl;
        assert(nextGen.size() == popSize);
        population.clear();
        population = nextGen;
        if (verbosity >= 3) cerr << "done completely" << endl;
    }

    int nsga2ParetoDominates(Individual<DNA>* a, Individual<DNA>* b)
    {
        int a_dominates = 0;
        int b_dominates = 0;

        for (auto o : a->fitnesses)
        {
            double fit_a = o.second;
            double fit_b = b->fitnesses[o.first];

            if (isBetter(fit_a, fit_b))         a_dominates = 1;
            else if (isBetter(fit_b, fit_a))    b_dominates = 1;
        }

        if (a_dominates >  b_dominates)         return  1;  // a dominates b
        if (a_dominates <  b_dominates)         return -1;  // b dominates a
        return 0;                                           // No one dominates the other
    }

    bool paretoDominates(const Individual<DNA> &a, const Individual<DNA> &b) const {
        for (auto &o : a.fitnesses)
            if (!isBetter(o.second, b.fitnesses.at(o.first))) return false;
        return true;
    }

    vector<Individual<DNA> *> getParetoFront(
            const std::vector<Individual<DNA> *> &ind) const {
        // naive algorithm. Should be ok for small ind.size()
        vector<Individual<DNA> *> pareto;
        for (size_t i = 0; i < ind.size(); ++i) {
            bool dominated = false;
            for (auto &j : pareto) {
                if (paretoDominates(*j, *ind[i])) {
                    dominated = true;
                    break;
                }
            }
            if (!dominated) {
                for (size_t j = i + 1; j < ind.size(); ++j) {
                    if (paretoDominates(*ind[j], *ind[i])) {
                        dominated = true;
                        break;
                    }
                }
                if (!dominated) {
                    pareto.push_back(ind[i]);
                }
            }
        }
        return pareto;
    }

    Individual<DNA> *paretoTournament() {
        std::uniform_int_distribution<size_t> dint(0, population.size() - 1);
        std::vector<Individual<DNA> *> participants;
        for (size_t i = 0; i < tournamentSize; ++i)
            participants.push_back(&population[dint(globalRand)]);
        auto pf = getParetoFront(participants);
        assert(pf.size() > 0);
        std::uniform_int_distribution<size_t> dpf(0, pf.size() - 1);
        return pf[dpf(globalRand)];
    }

    Individual<DNA> *randomObjTournament() {
        if (verbosity >= 3) cerr << "random obj tournament called" << endl;
        std::uniform_int_distribution<size_t> dint(0, population.size() - 1);
        std::vector<Individual<DNA> *> participants;
        for (size_t i = 0; i < tournamentSize; ++i)
            participants.push_back(&population[dint(globalRand)]);
        auto champion = participants[0];
        // we pick the objective randomly
        std::string obj;
        if (champion->fitnesses.size() == 1) {
            obj = champion->fitnesses.begin()->first;
        } else {
            std::uniform_int_distribution<int> dObj(
                    0, static_cast<int>(champion->fitnesses.size()) - 1);
            auto it = champion->fitnesses.begin();
            std::advance(it, dObj(globalRand));
            obj = it->first;
        }
        for (size_t i = 1; i < tournamentSize; ++i) {
            if (isBetter(participants[i]->fitnesses.at(obj), champion->fitnesses.at(obj)))
                champion = participants[i];
        }
        if (verbosity >= 3) cerr << "champion found" << endl;
        return champion;
    }

    std::vector<std::vector<Individual<DNA>*>> paretoFronts;
    void nsga2SortPopulation(std::vector<Individual<DNA>>& pop)
    {
        paretoFronts.clear();
        std::vector<Individual<DNA>*> currentFront;
        std::vector<Individual<DNA>*> lastFront;
        int currentRank = 1;
        int placedIndivs = 0;

        for (size_t pid = 0; pid < pop.size(); ++pid)
        {
            Individual<DNA>* p = &pop[pid];
            p->np = 0;
            p->sp.clear();

            for (size_t qid = 0; qid < pop.size(); ++qid)
            {
                if (pid == qid) continue;
                Individual<DNA>* q = &pop[qid];

                int cmp = nsga2ParetoDominates(p, q);
                if      (cmp > 0)   p->sp.push_back(q); // p dominates q
                else if (cmp < 0)   p->np++;            // q dominates p
            }

            if (p->np == 0)
            {
                currentFront.push_back(p);
                ++placedIndivs;
                p->paretoRank = currentRank;
            }
        }

        do
        {
            ++currentRank;
            if (!currentFront.empty())
            {
                paretoFronts.push_back(currentFront);
            }
            lastFront = currentFront;
            currentFront.clear();

            for (size_t pid = 0; pid < lastFront.size(); ++pid)
            {
                Individual<DNA>* p = lastFront[pid];

                for (auto q : p->sp)
                {
                    --q->np;

                    if (q->np == 0)
                    {
                        q->paretoRank = currentRank;
                        ++placedIndivs;
                        currentFront.push_back(q);
                    }
                }
            }
        } while (placedIndivs != pop.size());

        // Get all fitness names
        std::vector<std::string> fitnessNames;
        for (auto f : paretoFronts[0][0]->fitnesses)
        {
            fitnessNames.push_back(f.first);
        }

        // Compute crowding distances
        for (auto& f : paretoFronts)
        {
            size_t n = f.size();

            // init distance to 0
            for (auto ind : f)
            {
                ind->crowdingDistance = 0;
            }

            for (auto fit : fitnessNames)
            {
                // Sort population given fitness
                if (n > 1)
                {
                    std::sort(f.begin(), f.end(),
                            [&](Individual<DNA>* a, Individual<DNA>* b)
                            {
                            return isBetter(a->fitnesses[fit], b->fitnesses[fit]);
                            });

                    f[n-1]->crowdingDistance = std::numeric_limits<double>::infinity();
                }

                f[0]->crowdingDistance = std::numeric_limits<double>::infinity();

                double fmin = f[0]->fitnesses[fit];
                double fmax = f[n-1]->fitnesses[fit];
                double denom = fmax - fmin;

                for (size_t i = 1; i < n - 1; ++i)
                {
                    double a = f[i+1]->fitnesses[fit];
                    double b = f[i-1]->fitnesses[fit];
                    f[i]->crowdingDistance += (a - b) / denom;
                }
            }
        }

        nsga2SaveFront();
    }

    /*
    Individual<DNA>* nsga2Tournament()
    {
        std::uniform_int_distribution<size_t> dint(0, population.size() - 1);

        Individual<DNA>* a, b;

        a = &population[dint(globalRand)];
        b = &population[dint(globalRand)];

        return nsga2Tournament(a, b);
    }
    */

    Individual<DNA>* nsga2Tournament(Individual<DNA>* a, Individual<DNA>* b)
    {
        if      (a->paretoRank          < b->paretoRank)        return a;
        else if (b->paretoRank          < a->paretoRank)        return b;
        else if (a->crowdingDistance    > b->crowdingDistance)  return a;
        else if (b->crowdingDistance    > a->crowdingDistance)  return b;

        // Could not find the better guy, select randomly
        std::uniform_int_distribution<size_t> ht(0, 1);
        return (ht(globalRand) == 0 ? a : b);
    }

    vector<Individual<DNA>> getLastParetoFront()
    {
        vector<Individual<DNA>> result;
        if (selecMethod == SelectionMethod::nsga2Tournament)
        {
            auto saved_pop = population;
            population = lastGen;

            initializeNSGA2();

            for (auto ind : paretoFronts[0])
            {
                result.push_back(*ind);
            }

            population = saved_pop;
        }
        else
        {
            vector<Individual<DNA>*> pop;

            for (auto &p : lastGen) {
                pop.push_back(&p);
            }

            auto front = getParetoFront(pop);

            for (auto& p : front)
            {
                result.push_back(*p);
            }
        }
        return result;
    }

    unordered_map<string, vector<Individual<DNA>>> getElites(size_t n) {
        vector<string> obj;
        for (auto &o : population[0].fitnesses) obj.push_back(o.first);
        return getElites(obj, n, population);
    }
    unordered_map<string, vector<Individual<DNA>>> getLastGenElites(size_t n) {
        vector<string> obj;
        for (auto &o : population[0].fitnesses) obj.push_back(o.first);
        return getElites(obj, n, lastGen);
    }
    unordered_map<string, vector<Individual<DNA>>> getElites(
            const vector<string> &obj, size_t n, const vector<Individual<DNA>> &popVec) {


        if (verbosity >= 3) {
            cerr << "getElites : nbObj = " << obj.size() << " n = " << n << endl;
        }
        unordered_map<string, vector<Individual<DNA>>> elites;

        if (selecMethod == SelectionMethod::nsga2Tournament) return elites;
        for (auto &o : obj) {
            elites[o] = vector<Individual<DNA>>();
            elites[o].push_back(popVec[0]);
            size_t worst = 0;
            for (size_t i = 1; i < n && i < popVec.size(); ++i) {
                elites[o].push_back(popVec[i]);
                if (isBetter(elites[o][worst].fitnesses.at(o), popVec[i].fitnesses.at(o)))
                    worst = i;
            }
            for (size_t i = n; i < popVec.size(); ++i) {
                if (isBetter(popVec[i].fitnesses.at(o), elites[o][worst].fitnesses.at(o))) {
                    elites[o][worst] = popVec[i];
                    for (size_t j = 0; j < n; ++j) {
                        if (isBetter(elites[o][worst].fitnesses.at(o), elites[o][j].fitnesses.at(o)))
                            worst = j;
                    }
                }
            }
        }
        return elites;
    }

 protected:
    /*********************************************************************************
     *                          NOVELTY RELATED METHODS
     ********************************************************************************/
    // Novelty works with footprints. A footprint is just a vector of vector of doubles.
    // It is recommended that those doubles are within a same order of magnitude.
    // Each vector<double> is a "snapshot": it represents the state of the evaluation of
    // one individual at a certain time. Thus, a complete footprint is a combination
    // of one or more snapshot taken at different points in the
    // simulation (a vector<vector<double>>).
    // Snapshot must be of same size accross individuals.
    // Footprint must be set in the evaluator (see examples)

    static double getFootprintDistance(const fpType &f0, const fpType &f1) {
        assert(f0.size() == f1.size());
        double d = 0;
        for (size_t i = 0; i < f0.size(); ++i) {
            for (size_t j = 0; j < f0[i].size(); ++j) {
                d += std::pow(f0[i][j] - f1[i][j], 2);
            }
        }
        return sqrt(d);
    }

    // computeAvgDist (novelty related)
    // returns the average distance of a footprint fp to its k nearest neighbours
    // in an archive of footprints
    static double computeAvgDist(size_t K, const vector<Individual<DNA>> &arch,
            const fpType &fp) {
        double avgDist = 0;
        if (arch.size() > 1) {
            size_t k = arch.size() < K ? static_cast<size_t>(arch.size()) : K;
            vector<Individual<DNA>> knn;
            knn.reserve(k);
            vector<double> knnDist;
            knnDist.reserve(k);
            std::pair<double, size_t> worstKnn = {getFootprintDistance(fp, arch[0].footprint),
                0};  // maxKnn is the worst among the knn
            for (size_t i = 0; i < k; ++i) {
                knn.push_back(arch[i]);
                double d = getFootprintDistance(fp, arch[i].footprint);
                knnDist.push_back(d);
                if (d > worstKnn.first) {
                    worstKnn = {d, i};
                }
            }
            for (size_t i = k; i < arch.size(); ++i) {
                double d = getFootprintDistance(fp, arch[i].footprint);
                if (d < worstKnn.first) {  // this one is closer than our worst knn
                    knn[worstKnn.second] = arch[i];
                    knnDist[worstKnn.second] = d;
                    worstKnn.first = d;
                    // we update maxKnn
                    for (size_t j = 0; j < knn.size(); ++j) {
                        if (knnDist[j] > worstKnn.first) {
                            worstKnn = {knnDist[j], j};
                        }
                    }
                }
            }
            assert(knn.size() == k);
            for (size_t i = 0; i < knn.size(); ++i) {
                assert(getFootprintDistance(fp, knn[i].footprint) == knnDist[i]);
                avgDist += knnDist[i];
            }
            avgDist /= static_cast<double>(knn.size());
        }
        return avgDist;
    }
    void updateNovelty() {
        if (verbosity >= 2) {
            cout << endl << endl;
            std::stringstream output;
            cout << GREY << " ❯❯  " << YELLOW << "COMPUTING NOVELTY " << NORMAL << " ⤵  "
                << endl
                << endl;
        }
        auto savedArchiveSize = archive.size();
        for (auto &ind : population) {
            archive.push_back(ind);
        }
        std::pair<Individual<DNA> *, double> best = {&population[0], 0};
        vector<Individual<DNA>> toBeAdded;
        for (auto &ind : population) {
            double avgD = computeAvgDist(KNN, archive, ind.footprint);
            bool added = false;
            if (avgD > minNoveltyForArchive) {
                toBeAdded.push_back(ind);
                added = true;
            }
            if (avgD > best.second) best = {&ind, avgD};
            if (verbosity >= 2) {
                std::stringstream output;
                output << GREY << " ❯ " << endl
                    << NORMAL << ind.infos << endl
                    << " -> Novelty = " << CYAN << avgD << GREY
                    << (added ? " (added to archive)" : " (too low for archive)") << NORMAL;
                if (verbosity >= 3)
                    output << "Footprint was : " << footprintToString(ind.footprint);
                output << endl;
                std::cout << output.str();
            }
            ind.fitnesses["novelty"] = avgD;
        }
        archive.resize(savedArchiveSize);
        archive.insert(std::end(archive), std::begin(toBeAdded), std::end(toBeAdded));
        if (verbosity >= 2) {
            std::stringstream output;
            output << " Added " << toBeAdded.size() << " new footprints to the archive."
                << std::endl
                << "New archive size = " << archive.size() << " (was " << savedArchiveSize
                << ")." << std::endl;
            std::cout << output.str() << std::endl;
        }
        if (verbosity >= 2) {
            std::stringstream output;
            output << "Most novel individual (novelty = " << best.second
                << "): " << best.first->infos << endl;
            cout << output.str();
        }
    }

    // panpan cucul
    static inline string footprintToString(const vector<vector<double>> &f) {
        std::ostringstream res;
        res << "👣  " << json(f).dump();
        return res.str();
    }

    string selectMethodToString(const SelectionMethod &sm) {
        switch (sm) {
            case SelectionMethod::paretoTournament:
                return "pareto tournament";
            case SelectionMethod::randomObjTournament:
                return "random objective tournament";
            case SelectionMethod::nsga2Tournament:
                return "NSGA-II";
        }
        return "???";
    }

    /*********************************************************************************
     *                           STATS, LOGS & PRINTING
     ********************************************************************************/
#if not defined (NO_FANCY_OUTPUT)
    void printStart() {
        int nbCol = 55;
        std::cout << std::endl << GREY;
        for (int i = 0; i < nbCol - 1; ++i) std::cout << "━";
        std::cout << std::endl;
        std::cout << YELLOW << "              ☀     " << NORMAL << " Starting GAGA " << YELLOW
            << "    ☀ " << NORMAL;
        std::cout << std::endl;
        std::cout << BLUE << "                      ¯\\_ಠ ᴥ ಠ_/¯" << std::endl << GREY;
        for (int i = 0; i < nbCol - 1; ++i) std::cout << "┄";
        std::cout << std::endl << NORMAL;
        std::cout << "  ▹ population size = " << BLUE << popSize << NORMAL << std::endl;
        std::cout << "  ▹ nb of elites = " << BLUE << nbElites << NORMAL << std::endl;
        std::cout << "  ▹ nb of tournament competitors = " << BLUE << tournamentSize << NORMAL
            << std::endl;
        std::cout << "  ▹ selection = " << BLUE << selectMethodToString(selecMethod) << NORMAL
            << std::endl;
        std::cout << "  ▹ mutation rate = " << BLUE << mutationProba << NORMAL << std::endl;
        std::cout << "  ▹ crossover rate = " << BLUE << crossoverProba << NORMAL << std::endl;
        std::cout << "  ▹ writing results in " << BLUE << folder << NORMAL << std::endl;
        if (novelty) {
            std::cout << "  ▹ novelty is " << GREEN << "enabled" << NORMAL << std::endl;
            std::cout << "    - KNN size = " << BLUE << KNN << NORMAL << std::endl;
        } else {
            std::cout << "  ▹ novelty is " << RED << "disabled" << NORMAL << std::endl;
        }
#ifdef CLUSTER
        std::cout << "  ▹ MPI parallelisation is " << GREEN << "enabled" << NORMAL
            << std::endl;
#else
        std::cout << "  ▹ MPI parallelisation is " << RED << "disabled" << NORMAL
            << std::endl;
#endif
#ifdef OMP
        std::cout << "  ▹ OpenMP parallelisation is " << GREEN << "enabled" << NORMAL
            << std::endl;
#else
        std::cout << "  ▹ OpenMP parallelisation is " << RED << "disabled" << NORMAL
            << std::endl;
#endif
        std::cout << GREY;
        for (int i = 0; i < nbCol - 1; ++i) std::cout << "━";
        std::cout << NORMAL << std::endl;
    }
#else
    void printStart() {
        int nbCol = 55;
        printf("Starting GAGA... Infos:\n");
        printf("    Population size               %s%zu%s\n",     BLUE, popSize, NORMAL);
        printf("    Nb of elites                  %s%zu%s\n",     BLUE, nbElites, NORMAL);
        printf("    Nb of tournament competitors  %s%zu%s\n",     BLUE, tournamentSize, NORMAL);
        printf("    Selection                     %s%s%s\n",      BLUE, selectMethodToString(selecMethod).c_str(), NORMAL);
        printf("    Mutation rate                 %s%.3f%s\n",    BLUE, mutationProba, NORMAL);
        printf("    Crossover rate                %s%.3f%s\n",    BLUE, crossoverProba, NORMAL);
        printf("    Writing results to            %s%s%s\n",      BLUE, folder.c_str(), NORMAL);

        if (novelty)
        {
            printf("    Novelty is                    %senabled%s\n",   BLUE, NORMAL);
            printf("      - KNN size = %s%zu%s\n", BLUE, KNN, NORMAL);
        }
        else
        {
            printf("    Novelty is                    %sdisabled%s\n",   BLUE, NORMAL);
        }

#ifdef CLUSTER
        printf("    MPI parallelisation is        %senabled%s\n", GREEN, NORMAL);
#else               
        printf("    MPI parallelisation is        %sdisabled%s\n", RED, NORMAL);
#endif
#ifdef OMP
        printf("    OMP parallelisation is        %senabled%s\n", GREEN, NORMAL);
#else               
        printf("    OMP parallelisation is        %sdisabled%s\n, RED, NORMAL);
#endif
        printf("\n");
    }
#endif

    void updateStats(double totalTime) {
        // stats organisations :
        // "global" -> {"genTotalTime", "indTotalTime", "maxTime", "nEvals", "nObjs"}
        // "obj_i" -> {"avg", "worst", "best"}
        assert(population.size());
        std::map<std::string, std::map<std::string, double>> currentGenStats;
        currentGenStats["global"]["genTotalTime"] = totalTime;
        double indTotalTime = 0.0, maxTime = 0.0;
        int nEvals = 0;
        int nObjs = static_cast<int>(population[0].fitnesses.size());
        for (const auto &o : population[0].fitnesses) {
            currentGenStats[o.first] = {
                {{"avg", 0.0}, {"worst", o.second}, {"best", o.second}}};
        }
        for (const auto &ind : population) {
            indTotalTime += ind.evalTime;
            for (const auto &o : ind.fitnesses) {
                currentGenStats[o.first].at("avg") +=
                    (o.second / static_cast<double>(population.size()));
                if (isBetter(o.second, currentGenStats[o.first].at("best")))
                    currentGenStats[o.first].at("best") = o.second;
                if (!isBetter(o.second, currentGenStats[o.first].at("worst")))
                    currentGenStats[o.first].at("worst") = o.second;
            }
            if (ind.evalTime > maxTime) maxTime = ind.evalTime;
            if (!ind.wasAlreadyEvaluated) ++nEvals;
        }
        currentGenStats["global"]["indTotalTime"] = indTotalTime;
        currentGenStats["global"]["maxTime"] = maxTime;
        currentGenStats["global"]["nEvals"] = nEvals;
        currentGenStats["global"]["nObjs"] = nObjs;
        genStats.push_back(currentGenStats);
    }

#if not defined(NO_FANCY_OUTPUT)
    void printGenStats(size_t n) {
        const size_t l = 80;
        std::cout << tableHeader(l);
        std::ostringstream output;
        const auto &globalStats = genStats[n].at("global");
        output << "Generation " << CYANBOLD << n << NORMAL << " ended in " << BLUE
            << globalStats.at("genTotalTime") << NORMAL << "s";
        std::cout << tableCenteredText(l, output.str(), BLUEBOLD NORMAL BLUE NORMAL);
        output = std::ostringstream();
        output << GREYBOLD << "(" << globalStats.at("nEvals") << " evaluations, "
            << globalStats.at("nObjs") << " objs)" << NORMAL;
        std::cout << tableCenteredText(l, output.str(), GREYBOLD NORMAL);
        std::cout << tableSeparation(l);
        double timeRatio = 0;
        if (globalStats.at("genTotalTime") > 0)
            timeRatio = globalStats.at("indTotalTime") / globalStats.at("genTotalTime");
        output = std::ostringstream();
        output << "🕝  max: " << BLUE << globalStats.at("maxTime") << NORMAL << "s";
        output << ", 🕝  sum: " << BLUEBOLD << globalStats.at("indTotalTime") << NORMAL
            << "s (x" << timeRatio << " ratio)";
        std::cout << tableCenteredText(l, output.str(), CYANBOLD NORMAL BLUE NORMAL "      ");
        std::cout << tableSeparation(l);
        for (const auto &o : genStats[n]) {
            if (o.first != "global") {
                output = std::ostringstream();
                output << GREYBOLD << "--◇" << GREENBOLD << std::setw(10) << o.first << GREYBOLD
                    << " ❯ " << NORMAL << " worst: " << YELLOW << std::setw(12)
                    << o.second.at("worst") << NORMAL << ", avg: " << YELLOWBOLD
                    << std::setw(12) << o.second.at("avg") << NORMAL << ", best: " << REDBOLD
                    << std::setw(12) << o.second.at("best") << NORMAL;
                std::cout << tableText(l, output.str(),
                        "    " GREYBOLD GREENBOLD GREYBOLD NORMAL YELLOWBOLD NORMAL
                        YELLOW NORMAL GREENBOLD NORMAL);
            }
        }
        std::cout << tableFooter(l);
    }
#else
    void printGenStats(size_t n)
    {
        const auto &globalStats = genStats[n].at("global");

        printf("Generation %s%zu%s ended in %s%.4fs%s (%.0f evaluations, %.0f objectives)\n", CYANBOLD, n, NORMAL, BLUE, globalStats.at("genTotalTime"), NORMAL, globalStats.at("nEvals"), globalStats.at("nObjs"));

        double timeRatio = 0.0;
        if (globalStats.at("genTotalTime") > 0)
            timeRatio = globalStats.at("indTotalTime") / globalStats.at("genTotalTime");

        printf("    - timings : max %s%.3fs%s, sum %s%.3fs%s (x%.3f ratio)\n", BLUE, globalStats.at("maxTime"), NORMAL, BLUEBOLD, globalStats.at("indTotalTime"), NORMAL, timeRatio);
        printf("    - fitnesses :\n");
        
        for (const auto &o : genStats[n])
        {
            if (o.first != "global")
            {
                printf("        %s > worst %s%.3f%s, avg %s%.3f%s, best %s%.3f%s\n", o.first.c_str(), YELLOW, o.second.at("worst"), NORMAL, YELLOWBOLD, o.second.at("avg"), NORMAL, RED, o.second.at("best"), NORMAL);
            }
        }
        printf("\n");
    }
#endif

    void printIndividualStats(const Individual<DNA> &ind) {
        std::ostringstream output;
        output << GREYBOLD << "[" << YELLOW << procId << GREYBOLD << "]-▶ " << NORMAL;
        for (const auto &o : ind.fitnesses)
            output << " " << o.first << ": " << BLUEBOLD << std::setw(12) << o.second << NORMAL
                << GREYBOLD << " |" << NORMAL;
        output << " 🕝 : " << BLUE << ind.evalTime << "s" << NORMAL;
        if (ind.wasAlreadyEvaluated)
            output << GREYBOLD << " (already evaluated)\n" << NORMAL;
        else
            output << "\n";
        if ((!novelty && verbosity >= 2) || verbosity >= 3) output << ind.infos << std::endl;
        std::cout << output.str();
    }

    std::string tableHeader(unsigned int l) {
        std::ostringstream output;
        output << "┌";
        for (auto i = 0u; i < l; ++i) output << "─";
        output << "┓\n";
        return output.str();
    }

    std::string tableFooter(unsigned int l) {
        std::ostringstream output;
        output << "┗";
        for (auto i = 0u; i < l; ++i) output << "─";
        output << "┛\n";
        return output.str();
    }

    std::string tableSeparation(unsigned int l) {
        std::ostringstream output;
        output << "|" << GREYBOLD;
        for (auto i = 0u; i < l; ++i) output << "-";
        output << NORMAL << "|\n";
        return output.str();
    }

    std::string tableText(int l, std::string txt, std::string unprinted = "") {
        std::ostringstream output;
        int txtsize = static_cast<int>(txt.size() - unprinted.size());
        output << "|" << txt;
        for (auto i = txtsize; i < l; ++i) output << " ";
        output << "|\n";
        return output.str();
    }

    std::string tableCenteredText(int l, std::string txt, std::string unprinted = "") {
        std::ostringstream output;
        int txtsize = static_cast<int>(txt.size() - unprinted.size());
        int space = l - txtsize;
        output << "|";
        for (int i = 0; i < space / 2; ++i) output << " ";
        output << txt;
        for (int i = (space / 2) + txtsize; i < l; ++i) output << " ";
        output << "|\n";
        return output.str();
    }

    /*********************************************************************************
     *                         SAVING STUFF
     ********************************************************************************/
    void saveBests(size_t n) {
        if (n > 0) {
            // save n bests dnas for all objectives
            vector<string> objectives;
            for (auto &o : population[0].fitnesses) {
                objectives.push_back(o.first);  // we need to know objective functions
            }
            auto elites = getElites(objectives, n, population);
            std::stringstream baseName;
            baseName << folder << "/gen" << currentGeneration;
            fs::create_directory(baseName.str());
            if (verbosity >= 3) {
                cerr << "created directory " << baseName.str() << endl;
            }
            for (auto &e : elites) {
                int id = 0;
                for (auto &i : e.second) {
                    std::stringstream fileName;
                    fileName << baseName.str() << "/" << e.first << "_" << i.fitnesses.at(e.first)
                        << "_" << id++ << ".dna";
                    std::ofstream fs(fileName.str());
                    if (!fs) {
                        cerr << "Cannot open the output file." << endl;
                    }
                    fs << i.dna.serialize();
                    fs.close();
                }
            }
        }
    }

    void nsga2SaveFront()
    {
        // FIXME(charly): This is temporary and sucks hard, needs to be moved to folder
        std::string paretoFolder = "paretos";
        if (currentGeneration == 0)
        {
            fs::remove_all(paretoFolder);
            fs::create_directory(paretoFolder);
        }

        for (size_t i = 0; i < paretoFronts.size(); ++i)
        {
            std::string filename = paretoFolder + "/pareto_" + std::to_string(currentGeneration) + "_" + std::to_string(i) + ".dat";
            std::ofstream file(filename);

            for (auto ind : paretoFronts[i])
            {
                file << ind->fitnesses["f0"] << " " << ind->fitnesses["f1"] << "\n";
            }

            file.close();
        }
    }

    void saveParetoFront() {
        std::vector<Individual<DNA> *> pop;
        for (size_t i = 0; i < population.size(); ++i) {
            pop.push_back(&population[i]);
        }

        auto pfront = getParetoFront(pop);
        std::stringstream baseName;
        baseName << folder << "/gen" << currentGeneration;
        fs::create_directory(baseName.str());
        if (verbosity >= 3) {
            std::cout << "created directory " << baseName.str() << std::endl;
        }

        int id = 0;
        for (const auto &ind : pfront) {
            std::stringstream filename;
            filename << baseName.str() << "/";
            for (const auto &f : ind->fitnesses) {
                filename << f.first << f.second << "_";
            }
            filename << id++ << ".dna";

            std::ofstream fs(filename.str());
            if (!fs) {
                std::cerr << "Cannot open the output file.\n";
            }
            fs << ind->dna.serialize();
            fs.close();
        }
    }

    void saveGenStats() {
        std::stringstream csv;
        std::stringstream fileName;
        fileName << folder << "/gen_stats.csv";
        csv << "generation";
        if (genStats.size() > 0) {
            for (const auto &cat : genStats[0]) {
                std::stringstream column;
                column << cat.first << "_";
                for (const auto &s : cat.second) {
                    csv << "," << column.str() << s.first;
                }
            }
            csv << endl;
            for (size_t i = 0; i < genStats.size(); ++i) {
                csv << i;
                for (const auto &cat : genStats[i]) {
                    for (const auto &s : cat.second) {
                        csv << "," << s.second;
                    }
                }
                csv << endl;
            }
        }
        std::ofstream fs(fileName.str());
        if (!fs) cerr << "Cannot open the output file." << endl;
        fs << csv.str();
        fs.close();
    }

    // gen, idInd, fit0, fit1, time
    void saveIndStats() {
        std::stringstream csv;
        std::stringstream fileName;
        fileName << folder << "/ind_stats.csv";

        static bool indStatsWritten = false;

        if (!indStatsWritten) {
            csv << "generation,idInd,";
            for (auto &o : population[0].fitnesses) csv << o.first << ",";
            csv << "isOnParetoFront,time" << std::endl;
            indStatsWritten = true;
        }

        std::vector<int> isOnParetoFront(population.size(), false);
        if (selecMethod == SelectionMethod::paretoTournament)
        {
            std::vector<Individual<DNA>*> pop;
            for (auto &p : population) {
                pop.push_back(&p);
            }

            auto front = getParetoFront(pop);

            for (size_t i = 0; i < pop.size(); ++i) {
                Individual<DNA> *ind0 = pop[i];
                int found = 0;

                for (size_t j = 0; !found && (j < front.size()); ++j) {
                    Individual<DNA> *ind1 = front[j];

                    if (ind1 == ind0) {
                        found = 1;
                    }
                }

                isOnParetoFront[i] = found;
            }
        }

        for (size_t i = 0; i < population.size(); ++i) {
            const auto &p = population[i];
            csv << currentGeneration << "," << i << ",";
            for (auto &o : p.fitnesses) csv << o.second << ",";
            csv << isOnParetoFront[i] << "," << p.evalTime << std::endl;
        }
        std::ofstream fs;
        fs.open(fileName.str(), std::fstream::out | std::fstream::app);
        if (!fs) cerr << "Cannot open the output file." << endl;
        fs << csv.str();
        fs.close();
    }

    void saveIndStats_OneLinePerGen() {
        std::stringstream csv;
        std::stringstream fileName;
        fileName << folder << "/ind_stats.csv";

        static bool has_been_written = false;

        if (!has_been_written) {
            csv << "generation";
            size_t i = 0;
            for (const auto &ind : population) {
                csv << ",ind" << i++;
                for (const auto &f : ind.fitnesses) {
                    csv << "," << f.first;
                }
                csv << ",is_on_pareto_front,eval_time";
            }
            csv << endl;

            has_been_written = true;
        }

        std::vector<int> is_on_front(population.size(), false);

        if (selecMethod == SelectionMethod::paretoTournament) {
            std::vector<Individual<DNA> *> pop;

            for (auto &p : population) {
                pop.push_back(&p);
            }

            auto front = getParetoFront(pop);

            for (size_t i = 0; i < pop.size(); ++i) {
                Individual<DNA> *ind0 = pop[i];
                int found = 0;

                for (size_t j = 0; !found && (j < front.size()); ++j) {
                    Individual<DNA> *ind1 = front[j];

                    if (ind1 == ind0) {
                        found = 1;
                    }
                }

                is_on_front[i] = found;
            }
        }

        {
            csv << currentGeneration;
            size_t ind_id = 0;
            for (const auto &ind : population) {
                csv << "," << ind_id;
                for (const auto &f : ind.fitnesses) {
                    csv << "," << f.second;
                }

                csv << "," << is_on_front[ind_id];
                csv << "," << ind.evalTime;
                ++ind_id;
            }
            csv << endl;
        }

        std::ofstream fs;
        fs.open(fileName.str(), std::fstream::out | std::fstream::app);
        if (!fs) {
            cerr << "Cannot open the output file." << endl;
        }
        fs << csv.str();
        fs.close();
    }

    void createFolder(string baseFolder) {
        if (baseFolder.back() != '/') baseFolder += "/";
        fs::create_directory(baseFolder);
        auto now = system_clock::now();
        time_t now_c = system_clock::to_time_t(now);
        struct tm *parts = localtime(&now_c);

        std::stringstream fname;
        fname << evaluatorName << "_" << parts->tm_mday << "_" << parts->tm_mon + 1 << "_";
        int cpt = 0;
        std::stringstream ftot;
        do {
            ftot.clear();
            ftot.str("");
            ftot << baseFolder << fname.str() << cpt;
            cpt++;
        } while (fs::exists(ftot.str())); //stat(ftot.str().c_str(), &sb) == 0 && S_ISDIR(sb.st_mode));
        folder = ftot.str();
        fs::create_directory(folder);
    }

 public:
    void loadPop(string file) {
        std::ifstream t(file);
        std::stringstream buffer;
        buffer << t.rdbuf();
        auto o = json::parse(buffer.str());
        assert(o.count("population"));
        if (o.count("generation")) {
            currentGeneration = o.at("generation");
        } else {
            currentGeneration = 0;
        }
        population.clear();
        for (auto ind : o.at("population")) {
            population.push_back(Individual<DNA>(DNA(ind.at("dna"))));
            population[population.size() - 1].evaluated = false;
        }
    }

    void savePop() {
        json o = Individual<DNA>::popToJSON(population);
        o["evaluator"] = evaluatorName;
        o["generation"] = currentGeneration;
        std::stringstream baseName;
        baseName << folder << "/gen" << currentGeneration;
        fs::create_directory(baseName.str());
        std::stringstream fileName;
        fileName << baseName.str() << "/pop" << currentGeneration << ".pop";
        std::ofstream file;
        file.open(fileName.str());
        file << o.dump();
        file.close();
    }
    void saveArchive() {
        json o = Individual<DNA>::popToJSON(archive);
        o["evaluator"] = evaluatorName;
        std::stringstream baseName;
        baseName << folder << "/gen" << currentGeneration;
        fs::create_directory(baseName.str());
        std::stringstream fileName;
        fileName << baseName.str() << "/archive" << currentGeneration << ".pop";
        std::ofstream file;
        file.open(fileName.str());
        file << o.dump();
        file.close();
    }
};
}  // namespace GAGA
#endif
