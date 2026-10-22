/* usage: aubio_onsets file.f32 samplerate method bufsize hop [threshold] -> onset times in seconds, one per line */
#include <stdio.h>
#include <stdlib.h>
#include <aubio.h>
int main(int argc, char **argv) {
  if (argc < 6) return 1;
  FILE *f = fopen(argv[1], "rb"); if (!f) return 2;
  uint_t sr = atoi(argv[2]), bufsize = atoi(argv[4]), hop = atoi(argv[5]);
  aubio_onset_t *o = new_aubio_onset(argv[3], bufsize, hop, sr);
  if (!o) return 3;
  if (argc > 6) aubio_onset_set_threshold(o, atof(argv[6]));
  fvec_t *in = new_fvec(hop), *out = new_fvec(1);
  float *buf = malloc(sizeof(float) * hop);
  size_t n; long long total = 0;
  while ((n = fread(buf, sizeof(float), hop, f)) > 0) {
    for (uint_t i = 0; i < hop; i++) in->data[i] = i < n ? buf[i] : 0;
    aubio_onset_do(o, in, out);
    if (out->data[0] != 0) printf("%.4f\n", aubio_onset_get_last_s(o));
    total += n;
  }
  /* flush: feed a few silent hops so the delayed detections can come out */
  for (int k = 0; k < 8; k++) {
    for (uint_t i = 0; i < hop; i++) in->data[i] = 0;
    aubio_onset_do(o, in, out);
    if (out->data[0] != 0) printf("%.4f\n", aubio_onset_get_last_s(o));
  }
  del_aubio_onset(o); del_fvec(in); del_fvec(out); free(buf); fclose(f);
  return 0;
}
