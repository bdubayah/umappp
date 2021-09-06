#ifndef UMAPPP_NEIGHBOR_SIMILARITIES_HPP
#define UMAPPP_NEIGHBOR_SIMILARITIES_HPP

#include <vector>
#include <limits>
#include <cmath>
#include <algorithm>
#include <numeric>

#include "NeighborList.hpp"

namespace umappp {

inline void neighbor_similarities(
    NeighborList& x, 
    double local_connectivity = 1.0, 
    double bandwidth = 1.0,
    int max_iter = 64, 
    double tol = 1e-5, 
    double min_k_dist_scale = 1e-3
) {
    double grand_mean_dist = -1;
    constexpr double max_val = std::numeric_limits<double>::max();

    #pragma omp parallel
    {
        std::vector<double> non_zero_distances;
        
        #pragma omp for
        for (size_t i = 0; i < x.size(); ++i) {
            auto& all_neighbors = x[i];
            const int n_neighbors = all_neighbors.size();

            non_zero_distances.clear();
            for (const auto& f : all_neighbors) {
                if (f.second) {
                    non_zero_distances.push_back(f.second);
                }
            }

            if (non_zero_distances.size() <= local_connectivity) {
                // When this happens, 'rho' is just theoretically set to the
                // maximum distance. In such cases, the weights are always just
                // set to 1 in the remaining code, because no distance can be
                // greater than 'rho'. If that's the case, we might as well
                // save some time and compute it here.
                for (int k = 0; k < n_neighbors; ++k) {
                    all_neighbors[k].second = 1;
                }
                continue;
            }

            // Find rho, the distance to the nearest (non-identical) neighbor,
            // possibly with interpolation.
            int index = std::floor(local_connectivity);
            const double interpolation = local_connectivity - index;
            const double lower = (index > 0 ? non_zero_distances[index - 1] : 0); // 'index' is 1-based, so -1.
            const double upper = non_zero_distances[index];
            const double rho = lower + interpolation * (upper - lower);

            // Iterating to find a good sigma, just like how t-SNE does so for beta.
            double sigma = 1.0;
            double lo = 0.0;
            double hi = max_val;
            double sigma_best = sigma;
            double adiff_min = max_val;
            const double target = std::log2(all_neighbors.size() + 1); // include self. Dunno why, but uwot does it.

            bool converged = false;
            for (int iter = 0; iter < max_iter; ++iter) {
                // If distance = 0, then max(distance - rho, 0) = 0 as rho >=
                // 0. In which case, exp(-dist / sigma) is just 1 for each
                // distance of zero, allowing us to just add these directly.
                double val = n_neighbors - non_zero_distances.size();
                
                for (auto d : non_zero_distances) {
                    if (d > rho) {
                        val += std::exp(-(d - rho)/ sigma);
                    } else {
                        val += 1;
                    }
                }

                double adiff = std::abs(val - target);
                if (adiff < tol) {
                    converged = true;
                    break;
                }

                // store best sigma in case binary search fails (usually in the presence
                // of multiple degenerate distances)
                if (adiff < adiff_min) {
                    adiff_min = adiff;
                    sigma_best = sigma;
                }

                if (val > target) {
                    hi = sigma;
                    sigma = (lo + hi) / 2;
                } else {
                    lo = sigma;
                    if (hi == max_val) {
                        sigma *= 2;
                    } else {
                        sigma = (lo + hi) / 2;
                    }
                }
            }

            if (!converged) {
                sigma = sigma_best;
            }

            // Quickly summing over the non-zero distances, then dividing
            // by the total number of neighbors to obtain the mean.
            double mean_dist = std::accumulate(non_zero_distances.begin(), non_zero_distances.end(), 0.0)/n_neighbors;
            sigma = std::max(min_k_dist_scale * mean_dist, sigma);

            for (int k = 0; k < n_neighbors; ++k) {
                double& dist = all_neighbors[k].second;
                if (dist > rho) {
                    dist = std::exp(-(dist - rho) / (sigma * bandwidth));
                } else {
                    dist = 1;
                }
            }
        }
    }

    return;
}

}

#endif
