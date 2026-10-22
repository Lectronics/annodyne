// usage: sf_cli file.f32 samplerate [online 0/1] [threshold] [act_out.txt]
#include "superflux.hpp"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
  if (argc < 3) return 1;
  std::FILE* f = std::fopen(argv[1], "rb"); if (!f) return 2;
  std::vector<float> x; float v;
  while (std::fread(&v, sizeof(float), 1, f) == 1) x.push_back(v);
  std::fclose(f);
  superflux::Params p; p.sample_rate = std::atof(argv[2]);
  if (argc > 3) p.online = std::atoi(argv[3]) != 0;
  if (argc > 4) p.threshold = std::atof(argv[4]);
  std::vector<double> act;
  for (double t : superflux::detect(x, p, &act)) std::printf("%.4f\n", t);
  if (argc > 5) { std::FILE* g = std::fopen(argv[5], "w"); for (double a : act) std::fprintf(g, "%.9g\n", a); std::fclose(g); }
  return 0;
}
