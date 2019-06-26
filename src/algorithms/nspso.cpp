/* Copyright 2017-2018 PaGMO development team

This file is part of the PaGMO library.

The PaGMO library is free software; you can redistribute it and/or modify
it under the terms of either:

  * the GNU Lesser General Public License as published by the Free
    Software Foundation; either version 3 of the License, or (at your
    option) any later version.

or

  * the GNU General Public License as published by the Free Software
    Foundation; either version 3 of the License, or (at your option) any
    later version.

or both in parallel, as here.

The PaGMO library is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received copies of the GNU General Public License and the
GNU Lesser General Public License along with the PaGMO library.  If not,
see https://www.gnu.org/licenses/. */

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <pagmo/algorithm.hpp>
#include <pagmo/algorithms/nspso.hpp>
#include <pagmo/exceptions.hpp>
#include <pagmo/io.hpp>
#include <pagmo/population.hpp>
#include <pagmo/s11n.hpp>
#include <pagmo/types.hpp>
#include <pagmo/utils/generic.hpp>
#include <pagmo/utils/multi_objective.hpp>

namespace pagmo
{

nspso::nspso(unsigned gen, double min_w, double max_w, double c1, double c2, double chi, double v_coeff,
             unsigned leader_selection_range, std::string diversity_mechanism, unsigned seed)
    : m_gen(gen), m_min_w(min_w), m_max_w(max_w), m_c1(c1), m_c2(c2), m_chi(chi), m_v_coeff(v_coeff),
      m_leader_selection_range(leader_selection_range), m_diversity_mechanism(diversity_mechanism), m_velocity(),
      m_e(seed), m_seed(seed), m_verbosity(0u)
{
    if (min_w <= 0 || max_w <= 0 || c1 <= 0 || c2 <= 0 || chi <= 0) {
        pagmo_throw(std::invalid_argument,
                    "minimum and maximum particles' inertia weights, first and second magnitude of the force "
                    "coefficients and velocity scaling factor should be greater than 0");
    }
    if (min_w > max_w) {
        pagmo_throw(
            std::invalid_argument,
            "minimum particles' inertia weight should be lower than maximum particles' inertia weight, while values of"
                + std::to_string(min_w) + "and" + std::to_string(max_w) + ", respectively, were detected");
    }
    if (v_coeff <= 0 || v_coeff > 1) {
        pagmo_throw(std::invalid_argument, "velocity scaling factor should be in ]0,1] range, while a value of"
                                               + std::to_string(v_coeff) + " was detected");
    }
    if (leader_selection_range > 100) {
        pagmo_throw(std::invalid_argument,
                    "leader selection range coefficient should be in the ]0,100] range, while a value of"
                        + std::to_string(leader_selection_range) + " was detected");
    }
    if (diversity_mechanism != "crowding distance" && diversity_mechanism != "niche count"
        && diversity_mechanism != "max min") {

        pagmo_throw(std::invalid_argument, "Non existing diversity mechanism method.");
    }
}

/// Algorithm evolve method
/**
 * Evolves the population for the requested number of generations.
 *
 * @param pop population to be evolved
 * @return evolved population
 */
population nspso::evolve(population pop) const
{
    // We store some useful variables
    auto &prob = pop.get_problem(); // This is a const reference, so using set_seed for example will not be
                                    // allowed
    auto n_x = prob.get_nx();       // This getter does not return a const reference but a copy
    auto n_ix = prob.get_nix();
    auto n_cx = n_x - n_ix;
    auto bounds = prob.get_bounds();
    auto &lb = bounds.first;
    auto &ub = bounds.second;
    auto swarm_size = pop.size();
    unsigned count_verb = 1u; // regulates the screen output

    // PREAMBLE-------------------------------------------------------------------------------------------------
    // We start by checking that the problem is suitable for this
    // particular algorithm.
    if (prob.get_nx() == 0u) {
        pagmo_throw(std::invalid_argument, get_name() + " cannot work on problems without continuous part.");
    }
    if (prob.is_stochastic()) {
        pagmo_throw(std::invalid_argument,
                    "The problem appears to be stochastic " + get_name() + " cannot deal with it");
    }
    if (prob.get_nc() != 0u) {
        pagmo_throw(std::invalid_argument, "Non linear constraints detected in " + prob.get_name() + " instance. "
                                               + get_name() + " cannot deal with them.");
    }
    if (prob.get_nf() < 2u) {
        pagmo_throw(std::invalid_argument,
                    "This is a multi-objective algortihm, while number of objectives detected in " + prob.get_name()
                        + " is " + std::to_string(prob.get_nf()));
    }
    if (!pop.size()) {
        pagmo_throw(std::invalid_argument, get_name() + " does not work on an empty population");
    }
    // Get out if there is nothing to do.
    if (m_gen == 0u) {
        return pop;
    }
    // No throws, all valid: we clear the logs
    m_log.clear();

    vector_double dummy(n_x, 0.);       // used for initialisation purposes
    vector_double minv(n_x), maxv(n_x); // Maximum and minimum velocity allowed
    std::uniform_real_distribution<double> drng_real(0.0, 1.0);
    std::vector<vector_double::size_type> sort_list_2(swarm_size);
    std::vector<vector_double::size_type> sort_list_3(swarm_size);

    double vwidth; // Temporary variable
    // Initialise the minimum and maximum velocity
    for (decltype(n_x) i = 0u; i < n_x; ++i) {
        vwidth = (ub[i] - lb[i]) * m_v_coeff;
        minv[i] = -1. * vwidth;
        maxv[i] = vwidth;
    }

    // Initialize the particle velocities if necessary
    if ((m_velocity.size() != swarm_size)) { // with memory, add: || (!m_memory)
        m_velocity = std::vector<vector_double>(swarm_size, dummy);
        for (decltype(swarm_size) i = 0u; i < swarm_size; ++i) {
            for (decltype(n_x) j = 0u; j < n_x; ++j) {
                m_velocity[i][j] = uniform_real_from_range(minv[j], maxv[j], m_e);
            }
        }
    }

    // 0 - Copy the population into nextPopList
    std::vector<nspso_individual> next_pop_list;
    for (decltype(swarm_size) i = 0; i < swarm_size; ++i) {
        next_pop_list[i] = nspso_individual();
        next_pop_list[i].cur_x = pop.get_x()[i];
        next_pop_list[i].best_x = pop.get_x()[i];
        next_pop_list[i].cur_v = m_velocity[i];
        next_pop_list[i].cur_f = pop.get_f()[i];
        next_pop_list[i].best_f = pop.get_f()[i];
    }

    // Main NSPSO loop
    for (decltype(m_gen) gen = 1u; gen <= m_gen; gen++) {

        std::vector<vector_double::size_type> best_non_dom_indices;
        const auto &fit = pop.get_f();
        auto fnds_res = fast_non_dominated_sorting(fit);

        // 0 - Logs and prints (verbosity modes > 1: a line is added every m_verbosity generations)
        if (m_verbosity > 0u) {
            // Every m_verbosity generations print a log line
            if (gen % m_verbosity == 1u || m_verbosity == 1u) {
                // We compute the ideal point
                vector_double ideal_point_verb = ideal(pop.get_f());
                // Every 50 lines print the column names
                if (count_verb % 50u == 1u) {
                    print("\n", std::setw(7), "Gen:", std::setw(15), "Fevals:");
                    for (decltype(ideal_point_verb.size()) i = 0u; i < ideal_point_verb.size(); ++i) {
                        if (i >= 5u) {
                            print(std::setw(15), "... :");
                            break;
                        }
                        print(std::setw(15), "ideal" + std::to_string(i + 1u) + ":");
                    }
                    print('\n');
                }
                print(std::setw(7), gen, std::setw(15), prob.get_fevals());
                for (decltype(ideal_point_verb.size()) i = 0u; i < ideal_point_verb.size(); ++i) {
                    if (i >= 5u) {
                        break;
                    }
                    print(std::setw(15), ideal_point_verb[i]);
                }
                print('\n');
                ++count_verb;
                // Logs
                m_log.emplace_back(gen, prob.get_fevals(), ideal_point_verb);
            }
        }

        // 1 - Calculate non-dominated population
        if (m_diversity_mechanism == "crowding distance") {
            // This returns a std::tuple containing: -the non dominated fronts, -the domination list, -the domination
            // count, -the non domination rank
            auto ndf = std::get<0>(fnds_res);
            vector_double pop_cd(swarm_size); // crowding distances of the whole population
            best_non_dom_indices = sort_population_mo(fit);

        } else if (m_diversity_mechanism == "niche count") {
            std::vector<vector_double> non_dom_chromosomes;
            auto ndf = std::get<0>(fnds_res);

            for (decltype(non_dom_chromosomes.size()) i = 0; i < ndf[0].size(); ++i) {
                non_dom_chromosomes[i] = next_pop_list[ndf[0][i]].best_x;
            }

            vector_double nadir_point = nadir(fit);
            vector_double ideal_point = ideal(fit);

            // Fonseca-Fleming setting for delta
            double delta = 1.0;
            if (prob.get_nf() == 2) {
                delta = ((nadir_point[0] - ideal_point[0]) + (nadir_point[1] - ideal_point[1]))
                        / (non_dom_chromosomes.size() - 1);
            } else if (prob.get_nf() == 3) {
                double d1 = nadir_point[0] - ideal_point[0];
                double d2 = nadir_point[1] - ideal_point[1];
                double d3 = nadir_point[2] - ideal_point[2];
                double ndc_size = non_dom_chromosomes.size();
                delta = std::sqrt(4 * d2 * d1 * ndc_size + 4 * d3 * d1 * ndc_size + 4 * d2 * d3 * ndc_size
                                  + std::pow(d1, 2) + std::pow(d2, 2) + std::pow(d3, 2) - 2 * d2 * d1 - 2 * d3 * d1
                                  - 2 * d2 * d3 + d1 + d2 + d3)
                        / (2 * (ndc_size - 1));
            } else { // for higher dimension we just divide equally the entire volume containing the pareto front
                for (decltype(nadir_point.size()) i = 0; i < nadir_point.size(); ++i) {
                    delta *= nadir_point[i] - ideal_point[i];
                }
                delta = pow(delta, 1.0 / nadir_point.size()) / non_dom_chromosomes.size();
            }

            std::vector<vector_double::size_type> count(non_dom_chromosomes.size(), 0);
            std::vector<vector_double::size_type> sort_list(count.size());
            compute_niche_count(count, non_dom_chromosomes, delta);
            std::iota(std::begin(sort_list), std::end(sort_list), vector_double::size_type(0));
            std::sort(
                sort_list.begin(), sort_list.end(), [&count](decltype(count.size()) idx1, decltype(count.size()) idx2) {
                    return detail::less_than_f(static_cast<double>(count[idx1]), static_cast<double>(count[idx2]));
                });

            if (ndf[0].size() > 1) {
                for (unsigned int i = 0; i < sort_list.size(); ++i) {
                    best_non_dom_indices[i] = ndf[0][sort_list[i]];
                }
            } else { // ensure the non-dom population has at least 2 individuals (to avoid convergence to a point)
                unsigned min_pop_size = 2;
                for (decltype(ndf.size()) f = 0; min_pop_size > 0 && f < ndf.size(); ++f) {
                    for (unsigned int i = 0; min_pop_size > 0 && i < ndf[f].size(); ++i) {
                        best_non_dom_indices[i] = ndf[f][i];
                        min_pop_size--;
                    }
                }
            }
        } else { // m_diversity_method == MAXMIN
            vector_double maxmin(swarm_size, 0);
            compute_maxmin(maxmin, fit);
            std::iota(std::begin(sort_list_2), std::end(sort_list_2), vector_double::size_type(0));
            std::sort(sort_list_2.begin(), sort_list_2.end(),
                      [&maxmin](decltype(maxmin.size()) idx1, decltype(maxmin.size()) idx2) {
                          return detail::less_than_f(maxmin[idx1], maxmin[idx2]);
                      });
            best_non_dom_indices = sort_list_2;
            vector_double::size_type i;
            for (i = 1u; i < best_non_dom_indices.size() && maxmin[best_non_dom_indices[i]] < 0; ++i)
                ;
            if (i < 2) {
                i = 2; // ensure the non-dom population has at least 2 individuals (to avoid convergence to a point)
            }

            best_non_dom_indices = std::vector<vector_double::size_type>(
                best_non_dom_indices.begin(),
                best_non_dom_indices.begin()
                    + static_cast<vector_double::difference_type>(i)); // keep just the non dominated
        }

        // decrease W from maxW to minW troughout the run

        const double w = m_max_w - (m_max_w - m_min_w) / m_gen * gen;

        // 2 - Move the points

        for (vector_double::size_type idx = 0; idx < swarm_size; ++idx) {

            const vector_double &best_dvs = next_pop_list[idx].best_x;
            const vector_double &cur_dvs = next_pop_list[idx].cur_x;
            const vector_double &cur_vel = next_pop_list[idx].cur_v;

            // 2.1 - Calculate the leader

            unsigned ext
                = static_cast<unsigned>(ceil(best_non_dom_indices.size() * m_leader_selection_range / 100)) - 1;

            if (ext < 1) {
                ext = 1;
            }

            unsigned leader_idx;

            do {
                std::uniform_int_distribution<unsigned> drng(0, ext);
                leader_idx = drng(m_e); // to generate an integer number in [0, ext]
            } while (best_non_dom_indices[leader_idx] == idx);

            vector_double leader = next_pop_list[best_non_dom_indices[leader_idx]].best_x;

            // Calculate some random factors
            const double r1 = drng_real(m_e);
            const double r2 = drng_real(m_e);

            // Calculate new velocity and new position for each particle
            vector_double new_dvs(n_cx);
            vector_double new_vel(n_cx);

            for (decltype(cur_dvs.size()) i = 0; i < cur_dvs.size(); ++i) {

                double v = w * cur_vel[i] +

                           m_c1 * r1 * (best_dvs[i] - cur_dvs[i]) +

                           m_c2 * r2 * (leader[i] - cur_dvs[i]);

                if (v > maxv[i]) {
                    v = maxv[i];
                } else if (v < minv[i]) {
                    v = minv[i];
                }

                double x = cur_dvs[i] + m_chi * v;

                if (x > ub[i]) {
                    x = ub[i];
                    v = 0;

                } else if (x < lb[i]) {
                    x = lb[i];
                    v = 0;
                }

                new_vel[i] = v;
                new_dvs[i] = x;
            }

            // Add the moved particle to the population

            next_pop_list.push_back(nspso_individual());
            next_pop_list[idx + swarm_size].cur_x = new_dvs;
            next_pop_list[idx + swarm_size].best_x = new_dvs;
            next_pop_list[idx + swarm_size].cur_v = new_vel;
            next_pop_list[idx + swarm_size].cur_f = prob.fitness(new_dvs);
            next_pop_list[idx + swarm_size].best_f = next_pop_list[idx + swarm_size].cur_f;
        }

        // 3 - Select the best swarm_size individuals in the new population (of size 2*swarm_size) according to pareto
        // dominance

        std::vector<vector_double> next_pop_fit(next_pop_list.size());

        for (decltype(next_pop_list.size()) i = 0; i < next_pop_list.size(); ++i) {
            next_pop_fit[i] = next_pop_list[i].best_f;
        }

        std::vector<vector_double::size_type> best_next_pop_indices(swarm_size, 0);

        if (m_diversity_mechanism != "max min") {

            auto fnds_res_next = fast_non_dominated_sorting(next_pop_fit);
            auto ndf_next = std::get<0>(fnds_res);
            auto dl_next = std::get<1>(fnds_res);
            auto dc_next = std::get<2>(fnds_res);
            auto ndr_next = std::get<3>(fnds_res);

            for (decltype(ndf_next.size()) f = 0, i = 0; i < swarm_size && f < ndf_next.size(); ++f) {

                if (ndf_next[f].size() < swarm_size - i) { // then push the whole front in the population

                    for (decltype(ndf_next[f].size()) j = 0; j < ndf_next[f].size(); ++j) {

                        best_next_pop_indices[i] = ndf_next[f][j];
                        ++i;
                    }

                } else {

                    std::shuffle(ndf_next[f].begin(), ndf_next[f].end(), m_e);

                    for (decltype(swarm_size) j = 0; i < swarm_size; ++j) {
                        best_next_pop_indices[i] = ndf_next[f][j];
                        ++i;
                    }
                }
            }

        } else {

            vector_double maxmin(2 * swarm_size, 0);

            compute_maxmin(maxmin, next_pop_fit);
            // I extract the index list of maxmin sorted:
            std::iota(std::begin(sort_list_3), std::end(sort_list_3), vector_double::size_type(0));
            std::sort(sort_list_3.begin(), sort_list_3.end(),
                      [&maxmin](decltype(maxmin.size()) idx1, decltype(maxmin.size()) idx2) {
                          return detail::less_than_f(maxmin[idx1], maxmin[idx2]);
                      });
            best_next_pop_indices = std::vector<vector_double::size_type>(
                sort_list_3.begin(), sort_list_3.begin() + static_cast<vector_double::difference_type>(swarm_size));
        }

        // The next_pop_list for the next generation will contain the best swarm_size individuals out of 2*swarm_size
        // according to pareto dominance

        std::vector<nspso_individual> tmp_pop(swarm_size);

        for (decltype(swarm_size) i = 0; i < swarm_size; ++i) {
            tmp_pop[i] = next_pop_list[best_next_pop_indices[i]];
        }
        next_pop_list = tmp_pop;
        // 4 - I now copy insert the new population

        for (decltype(swarm_size) i = 0; i < swarm_size; ++i) {
            pop.set_xf(i, next_pop_list[i].cur_x, next_pop_list[i].cur_f);
        }

    } // end of main NSPSO loop

    return pop;
}

/// Sets the seed
/**
 * @param seed the seed controlling the algorithm stochastic behaviour
 */
void nspso::set_seed(unsigned seed)
{
    m_e.seed(seed);
    m_seed = seed;
}

/// Extra info
/**
 * Returns extra information on the algorithm.
 *
 * @return an <tt> std::string </tt> containing extra info on the algorithm
 */
std::string nspso::get_extra_info() const
{
    std::ostringstream ss;
    stream(ss, "\tGenerations: ", m_gen);
    stream(ss, "\n\tMinimum particles' inertia weight: ", m_min_w);
    stream(ss, "\n\tMaximum particles' inertia weight: ", m_max_w);
    stream(ss, "\n\tFirst magnitude of the force coefficients: ", m_c1);
    stream(ss, "\n\tSecond magnitude of the force coefficients: ", m_c2);
    stream(ss, "\n\tVelocity scaling factor: ", m_chi);
    stream(ss, "\n\tVelocity coefficient: ", m_v_coeff);
    stream(ss, "\n\tLeader selection range: ", m_leader_selection_range);
    stream(ss, "\n\tDiversity mechanism: ", m_diversity_mechanism);
    stream(ss, "\n\tSeed: ", m_seed);
    stream(ss, "\n\tVerbosity: ", m_verbosity);
    return ss.str();
}

/// Object serialization
/**
 * This method will save/load \p this into the archive \p ar.
 *
 * @param ar target archive.
 *
 * @throws unspecified any exception thrown by the serialization of the UDP and of primitive types.
 */
template <typename Archive>
void nspso::serialize(Archive &ar, unsigned)
{
    detail::archive(ar, m_gen, m_min_w, m_max_w, m_c1, m_c2, m_chi, m_v_coeff, m_leader_selection_range,
                    m_diversity_mechanism, m_e, m_seed, m_verbosity, m_log);
}

double nspso::minfit(vector_double::size_type(i), vector_double::size_type(j),
                     const std::vector<vector_double> &fit) const

{

    double min = fit[i][0] - fit[j][0];

    for (decltype(fit[i].size()) f = 0; f < fit[i].size(); ++f) {

        double tmp = fit[i][f] - fit[j][f];

        if (tmp < min) {

            min = tmp;
        }
    }

    return min;
}

void nspso::compute_maxmin(vector_double &maxmin, const std::vector<vector_double> &fit) const

{

    decltype(fit.size()) np = fit.size();

    for (decltype(np) i = 0; i < np; ++i) {

        maxmin[i] = minfit(i, (i + 1) % np, fit);

        for (decltype(np) j = 0; j < np; ++j) {

            if (i != j) {

                double tmp = minfit(i, j, fit);

                if (tmp > maxmin[i]) {

                    maxmin[i] = tmp;
                }
            }
        }
    }
}

double nspso::euclidian_distance(const vector_double &x, const vector_double &y) const

{
    double sum = 0.0;

    for (decltype(x.size()) i = 0; i < x.size(); ++i) {

        sum += pow(x[i] - y[i], 2);
    }

    return sqrt(sum);
}

void nspso::compute_niche_count(std::vector<vector_double::size_type> &count,
                                const std::vector<vector_double> &chromosomes, double delta) const

{

    std::fill(count.begin(), count.end(), 0);

    for (decltype(chromosomes.size()) i = 0; i < chromosomes.size(); ++i) {

        for (decltype(chromosomes.size()) j = 0; j < chromosomes.size(); ++j) {

            if (euclidian_distance(chromosomes[i], chromosomes[j]) < delta) {

                ++count[i];
            }
        }
    }
}

} // namespace pagmo

PAGMO_S11N_ALGORITHM_IMPLEMENT(pagmo::nspso)
