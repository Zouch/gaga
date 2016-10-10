#define OMP

#include "../gaga.hpp"
#include "../include/json.hpp"

#include <random>

using RealDistribution = std::uniform_real_distribution<>;
using IntDistribution  = std::uniform_int_distribution<int>;

static std::random_device   rd;
static std::mt19937_64      gen(rd());
static RealDistribution     rdis;
static IntDistribution      idis;

struct TestDNA
{
    double v0;
    double v1;

    TestDNA()
    {
        v0 = rdis(gen);
        v1 = rdis(gen);
    }

    explicit TestDNA(const std::string& js)
    {
        auto o = nlohmann::json::parse(js);
        v0 = o["v0"];
        v1 = o["v1"];
    }

    void mutate()
    {
        int v = idis(gen);
        if (v == 0)
        {
            v0 = rdis(gen);
        }
        else
        {
            v1 = rdis(gen);
        }
    }

    TestDNA crossover(const TestDNA& other)
    {
        TestDNA result;

        int v = idis(gen);
        if (v == 0)
        {
            result.v0 = v0;
            result.v1 = other.v1;
        }
        else
        {
            result.v0 = other.v0;
            result.v1 = v1;
        }

        return result;
    }

    void reset()
    {
    }

    std::string serialize() const
    {
        nlohmann::json o;
        o["v0"] = v0;
        o["v1"] = v1;
        return o.dump(2);
    }

    static TestDNA random()
    {
        TestDNA d;
        return d;
    }
};

int main(int argc, char** argv)
{
    GAGA::GA<TestDNA> ga(argc, argv);
    ga.setSaveFolder("evos");
    ga.setVerbosity(1);
    ga.setSelectionMethod(GAGA::SelectionMethod::nsga2Tournament);
    ga.setIsBetterMethod([](auto a, auto b) { return a < b; });
    ga.setEvaluator([](auto& i)
                    {
                        double f0 = i.dna.v0;
                        double g = 1 + 9.0 * i.dna.v1;
                        double f1 = g * (1 - std::sqrt(f0 / g));

                        //i.fitnesses["f0"] = i.dna.v0;
                        //i.fitnesses["f1"] = i.dna.v1;
                        i.fitnesses["f0"] = f0;
                        i.fitnesses["f1"] = f1;
                    });

    ga.setPopSize(200);
    ga.initPopulation([]() { return TestDNA::random(); });
    ga.step(1000);

    return 0;
}

