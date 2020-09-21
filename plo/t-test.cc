#include <stdio.h>

#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <utility>
#include <vector>


using std::cout;
using std::endl;
using std::fixed;
using std::ifstream;
using std::left;
using std::pair;
using std::right;
using std::scientific;
using std::setw;
using std::setprecision;
using std::string;
using std::vector;

#include <boost/math/distributions/students_t.hpp>

using boost::math::students_t;

pair<double, double> get_mean_and_standard_deviation(const vector<double> &vv) {
  const double sum = std::accumulate(vv.begin(), vv.end(), 0.0);
  const double mean = sum / vv.size();
  double A = 0;
  std::for_each(vv.begin(), vv.end(), [&mean, &A](const double v) {
					A += (v - mean) * (v - mean);
				      });
  return std::make_pair(mean, std::sqrt(A / (vv.size() - 1)));
}

vector<double> get_data_set(const string &fn) {
  ifstream fin(fn);
  if (!fin.good()) {
    fprintf(stderr, "Open file error: %s\n", fn.c_str());
    return vector<double>();
  }
  vector<double> result;
  for (string line; std::getline(fin, line) ; ) {
    result.push_back(std::stof(line));
  }
  return result;
}


void two_samples_t_test_equal_sd(
        double Sm1, // Sm1 = Sample Mean 1.
        double Sd1,   // Sd1 = Sample Standard Deviation 1.
        unsigned Sn1,   // Sn1 = Sample Size 1.
        double Sm2,   // Sm2 = Sample Mean 2.
        double Sd2,   // Sd2 = Sample Standard Deviation 2.
        unsigned Sn2,   // Sn2 = Sample Size 2.
        double alpha)   // alpha = Significance Level.
{
   // A Students t test applied to two sets of data.
   // We are testing the null hypothesis that the two
   // samples have the same mean and that any difference
   // if due to chance.
   // See http://www.itl.nist.gov/div898/handbook/eda/section3/eda353.htm
   //
   using namespace std;
   // using namespace boost::math;

   using boost::math::students_t;

   double v = (Sn1 + Sn2 - 2) ;
   double sp = sqrt(((Sn1-1) * Sd1 * Sd1 + (Sn2-1) * Sd2 * Sd2) / v);
   // t-statistic:
   double t_stat = (Sm1 - Sm2) / (sp * sqrt(1.0 / Sn1 + 1.0 / Sn2));
   cout << setw(20) << left << "T Statistic" << "=  " << t_stat << "\n";
   students_t dist(v);
   double q = cdf(complement(dist, fabs(t_stat)));
   cout << setw(20) << left << "P-value" << "=  "
	<< setprecision(3) << scientific << 2 * q << "\n";
   if(q < alpha / 2) {
      cout << "Sample 1 Mean != Sample 2 Mean\n";
   } else {
     printf("Sample 1 Mean = %.2f\n", Sm1);
     printf("Sample 2 Mean = %.2f\n", Sm2);
     cout << "Sample 1 Mean == Sample 2 Mean\n";
     return;
   }
   printf("Sample 1 Mean = %.2f\n", Sm1);
   printf("Sample 2 Mean = %.2f\n", Sm2);

   if(cdf(dist, t_stat) < alpha) {
     printf("Sample 1 Mean <  Sample 2 Mean\n");
     printf("Sample improvement = %.2f%%\n", (Sm2 - Sm1) / Sm1 * 100);
   }
   if(cdf(complement(dist, t_stat)) < alpha) {
     printf("Sample 1 Mean >  Sample 2 Mean\n");
     printf("Sample regression = %.2f%%\n", (Sm1 - Sm2) / Sm1 * 100);
   }
}

double confidence_interval(const vector<double> &data) {
  students_t dist(data.size() - 1);
  double t_star = quantile(complement(dist, 0.05 / 2));
  auto mean_and_standard_deviation = get_mean_and_standard_deviation(data);
  return t_star * mean_and_standard_deviation.second / sqrt(data.size());
}

int main(const int argc, const char *argv[]) {

  if (argc < 3) {
    fprintf(stderr, "Missing argument\n");
    return 1;
  }
  
   vector<double> data_1 = get_data_set(argv[1]);
   vector<double> data_2 = get_data_set(argv[2]);

   if (data_1.empty() || data_2.empty()) {
     fprintf(stderr, "Empty data set(s).\n");
     return 1;
   }

   if (data_1.size() != data_2.size()) {
     fprintf(stderr, "Data sets have different number of data points.\n");
     return 1;
   }

   vector<double> diff_set;
   for(vector<double>::iterator i = data_1.begin(),
	 j = data_2.begin(), e = data_1.end(); i != e; ++i, ++j)
     diff_set.push_back(*j - *i);

   auto p1 = get_mean_and_standard_deviation(data_1);
   auto p2 = get_mean_and_standard_deviation(data_2);
   auto p3 = get_mean_and_standard_deviation(diff_set);
   double diff_standard_error = p3.second / sqrt(diff_set.size());
   // T value
   double diff_t = p3.first / diff_standard_error;
   students_t dist(data_1.size() - 1);
   // P = 2 * q;
   double q = cdf(complement(dist, fabs(diff_t)));

   fprintf(stderr, "Group 1 mean = %.2f ± %.2f\n", p1.first, confidence_interval(data_1));
   fprintf(stderr, "Group 2 mean = %.2f ± %.2f\n", p2.first, confidence_interval(data_2));
   if (q * 2 <= 0.01) {
     fprintf(stderr, "P value      = %.2e\n", q * 2);
   } else {
     fprintf(stderr, "P value      = %.2f\n", q * 2);
   }
   if (q * 2 > 0.05) {
     fprintf(stderr, "Difference is not significant.\n");
     return 0;
   }
   double w = confidence_interval(diff_set);
   fprintf(stderr, "Diff mean (95%% CI)  = %.2f ± %.2f\n", p3.first, w);
   fprintf(stderr, "Percent   (95%% CI) = %.2f%% (± %.2f%%)\n", p3.first / p1.first * 100, w / p1.first * 100);

   return 0;
} // int main()
