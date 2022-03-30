#ifndef CONSTI10_DRMPRIME_OUT
#define CONSTI10_DRMPRIME_OUT

struct AVFrame;
typedef struct drmprime_out_env_s drmprime_out_env_t;

int drmprime_out_display(drmprime_out_env_t * dpo, struct AVFrame * frame);
void drmprime_out_delete(drmprime_out_env_t * dpo);
drmprime_out_env_t * drmprime_out_new(int renderMode);

static int CALCULATOR_LOG_INTERVAL=10;

#endif //CONSTI10_DRMPRIME_OUT