// Original Author: Andrej Karpathy
// https://github.com/karpathy/llm.c

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include <unistd.h>

#include "thread.h"
#include "thread-sync.h"

// ----------------------------------------------------------------------------
// all the individual layers' forward passes
// B = batch_size, T = sequence_length, C = channels, V = vocab_size

// 全局参数结构体
struct encoder_params {
    float* out;
    int* inp;
    float* wte;
    float* wpe;
    int B, T, C;
    int nthreads;
} encoder_args;

void encoder_forward_worker(int id) {
    int B = encoder_args.B, T = encoder_args.T, C = encoder_args.C;
    int n = encoder_args.nthreads;
    int b_per_thread = (B + n - 1) / n;
    int b_start = id * b_per_thread;
    int b_end = (b_start + b_per_thread > B) ? B : b_start + b_per_thread;

    for (int b = b_start; b < b_end; b++) {
        for (int t = 0; t < T; t++) {
            float* out_bt = encoder_args.out + b * T * C + t * C;
            int ix = encoder_args.inp[b * T + t];
            float* wte_ix = encoder_args.wte + ix * C;
            float* wpe_t = encoder_args.wpe + t * C;
            for (int i = 0; i < C; i++) {
                out_bt[i] = wte_ix[i] + wpe_t[i];
            }
        }
    }
}

void encoder_forward(float* out, int* inp, float* wte, float* wpe,
                              int B, int T, int C, int nthreads) {
    encoder_args = (struct encoder_params){ out, inp, wte, wpe, B, T, C, nthreads };
    for (int i = 0; i < nthreads; i++) {
        create(encoder_forward_worker);
    }
    join();
}


struct layernorm_params {
    float* out; float* mean; float* rstd;
    float* inp; float* weight; float* bias;
    int B, T, C;
    int nthreads;
} layernorm_args;

void layernorm_worker(int id) {
    int B = layernorm_args.B, T = layernorm_args.T, C = layernorm_args.C;
    int n = layernorm_args.nthreads;
    int b_per_thread = (B + n - 1) / n;
    int b_start = id * b_per_thread;
    int b_end = (b_start + b_per_thread > B) ? B : b_start + b_per_thread;
    float eps = 1e-5f;

    for (int b = b_start; b < b_end; b++) {
        for (int t = 0; t < T; t++) {
            float* x = layernorm_args.inp + b * T * C + t * C;
            float m = 0.0f;
            for (int i = 0; i < C; i++) m += x[i];
            m /= C;
            float v = 0.0f;
            for (int i = 0; i < C; i++) {
                float shift = x[i] - m;
                v += shift * shift;
            }
            v /= C;
            float s = 1.0f / sqrtf(v + eps);
            float* out_bt = layernorm_args.out + b * T * C + t * C;
            for (int i = 0; i < C; i++) {
                float n = s * (x[i] - m);
                out_bt[i] = n * layernorm_args.weight[i] + layernorm_args.bias[i];
            }
            layernorm_args.mean[b * T + t] = m;
            layernorm_args.rstd[b * T + t] = s;
        }
    }
}

void layernorm_forward(float* out, float* mean, float* rstd,
                                float* inp, float* weight, float* bias,
                                int B, int T, int C, int nthreads) {
    layernorm_args = (struct layernorm_params){ out, mean, rstd, inp, weight, bias, B, T, C, nthreads };
    for (int i = 0; i < nthreads; i++) create(layernorm_worker);
    join();
}


struct matmul_params {
    float* out; float* inp; float* weight; float* bias;
    int B, T, C, OC;
    int nthreads;
} matmul_args;

void matmul_worker(int id) {
    int B = matmul_args.B, T = matmul_args.T, C = matmul_args.C, OC = matmul_args.OC;
    int n = matmul_args.nthreads;
    int b_per_thread = (B + n - 1) / n;
    int b_start = id * b_per_thread;
    int b_end = (b_start + b_per_thread > B) ? B : b_start + b_per_thread;

    for (int b = b_start; b < b_end; b++) {
        for (int t = 0; t < T; t++) {
            float* out_bt = matmul_args.out + b * T * OC + t * OC;
            float* inp_bt = matmul_args.inp + b * T * C + t * C;
            for (int o = 0; o < OC; o++) {
                float val = (matmul_args.bias != NULL) ? matmul_args.bias[o] : 0.0f;
                float* wrow = matmul_args.weight + o * C;
                for (int i = 0; i < C; i++) {
                    val += inp_bt[i] * wrow[i];
                }
                out_bt[o] = val;
            }
        }
    }
}

void matmul_forward(float* out, float* inp, float* weight, float* bias,
                             int B, int T, int C, int OC, int nthreads) {
    matmul_args = (struct matmul_params){ out, inp, weight, bias, B, T, C, OC, nthreads };
    for (int i = 0; i < nthreads; i++) create(matmul_worker);
    join();
}


struct attn_params {
    float* out; float* preatt; float* att; float* inp;
    int B, T, C, NH;
    int nthreads;
} attn_args;

void attention_worker(int id) {
    int B = attn_args.B, T = attn_args.T, C = attn_args.C, NH = attn_args.NH;
    int hs = C / NH;
    float scale = 1.0f / sqrtf(hs);
    int n = attn_args.nthreads;
    int b_per_thread = (B + n - 1) / n;
    int b_start = id * b_per_thread;
    int b_end = (b_start + b_per_thread > B) ? B : b_start + b_per_thread;

    for (int b = b_start; b < b_end; b++) {
        for (int t = 0; t < T; t++) {
            for (int h = 0; h < NH; h++) {
                float* query_t = attn_args.inp + b * T * C * 3 + t * C * 3 + h * hs;
                float* preatt_bth = attn_args.preatt + b * NH * T * T + h * T * T + t * T;
                float* att_bth = attn_args.att + b * NH * T * T + h * T * T + t * T;

                float maxval = -10000.0f;
                for (int t2 = 0; t2 <= t; t2++) {
                    float* key_t2 = attn_args.inp + b * T * C * 3 + t2 * C * 3 + h * hs + C;
                    float val = 0.0f;
                    for (int i = 0; i < hs; i++) val += query_t[i] * key_t2[i];
                    val *= scale;
                    preatt_bth[t2] = val;
                    if (val > maxval) maxval = val;
                }

                float expsum = 0.0f;
                for (int t2 = 0; t2 <= t; t2++) {
                    float expv = expf(preatt_bth[t2] - maxval);
                    att_bth[t2] = expv;
                    expsum += expv;
                }
                float inv = 1.0f / expsum;
                for (int t2 = 0; t2 < T; t2++) att_bth[t2] = (t2 <= t) ? att_bth[t2] * inv : 0.0f;

                float* out_bth = attn_args.out + b * T * C + t * C + h * hs;
                for (int i = 0; i < hs; i++) out_bth[i] = 0.0f;
                for (int t2 = 0; t2 <= t; t2++) {
                    float* value_t2 = attn_args.inp + b * T * C * 3 + t2 * C * 3 + h * hs + C * 2;
                    float alpha = att_bth[t2];
                    for (int i = 0; i < hs; i++) {
                        out_bth[i] += alpha * value_t2[i];
                    }
                }
            }
        }
    }
}

void attention_forward(float* out, float* preatt, float* att, float* inp,
                                int B, int T, int C, int NH, int nthreads) {
    attn_args = (struct attn_params){ out, preatt, att, inp, B, T, C, NH, nthreads };
    for (int i = 0; i < nthreads; i++) create(attention_worker);
    join();
}


#define GELU_SCALING_FACTOR sqrtf(2.0f / M_PI)
struct gelu_params { float* out; float* inp; int N; int nthreads; } gelu_args;
void gelu_worker(int id) {
    int N = gelu_args.N, n = gelu_args.nthreads;
    int per = (N + n - 1) / n, start = id * per, end = (start + per > N) ? N : start + per;
    for (int i = start; i < end; i++) {
        float x = gelu_args.inp[i];
        float cube = 0.044715f * x * x * x;
        gelu_args.out[i] = 0.5f * x * (1.0f + tanhf(GELU_SCALING_FACTOR * (x + cube)));
    }
}
void gelu_forward(float* out, float* inp, int N, int nthreads) {
    gelu_args = (struct gelu_params){ out, inp, N, nthreads };
    for (int i = 0; i < nthreads; i++) create(gelu_worker);
    join();
}


struct residual_params { float* out; float* in1; float* in2; int N; int nthreads; } res_args;
void residual_worker(int id) {
    int N = res_args.N, n = res_args.nthreads;
    int per = (N + n - 1) / n, start = id * per, end = (start + per > N) ? N : start + per;
    for (int i = start; i < end; i++) res_args.out[i] = res_args.in1[i] + res_args.in2[i];
}
void residual_forward(float* out, float* in1, float* in2, int N, int nthreads) {
    res_args = (struct residual_params){ out, in1, in2, N, nthreads };
    for (int i = 0; i < nthreads; i++) create(residual_worker);
    join();
}


struct softmax_params { float* probs; float* logits; int B, T, V, nthreads; } softmax_args;
void softmax_worker(int id) {
    int B = softmax_args.B, T = softmax_args.T, V = softmax_args.V, n = softmax_args.nthreads;
    int b_per = (B + n - 1) / n, b_start = id * b_per, b_end = (b_start + b_per > B) ? B : b_start + b_per;

    for (int b = b_start; b < b_end; b++) {
        for (int t = 0; t < T; t++) {
            float* logits_bt = softmax_args.logits + b * T * V + t * V;
            float* probs_bt = softmax_args.probs + b * T * V + t * V;
            float maxval = -10000.0f;
            for (int i = 0; i < V; i++) if (logits_bt[i] > maxval) maxval = logits_bt[i];
            float sum = 0.0f;
            for (int i = 0; i < V; i++) {
                probs_bt[i] = expf(logits_bt[i] - maxval);
                sum += probs_bt[i];
            }
            for (int i = 0; i < V; i++) probs_bt[i] /= sum;
        }
    }
}
void softmax_forward(float* probs, float* logits, int B, int T, int V, int nthreads) {
    softmax_args = (struct softmax_params){ probs, logits, B, T, V, nthreads };
    for (int i = 0; i < nthreads; i++) create(softmax_worker);
    join();
}


// ----------------------------------------------------------------------------
// GPT-2 model definition

// the parameters of the model
#define NUM_PARAMETER_TENSORS 16
typedef struct {
    float* wte; // (V, C)
    float* wpe; // (maxT, C)
    float* ln1w; // (L, C)
    float* ln1b; // (L, C)
    float* qkvw; // (L, 3*C, C)
    float* qkvb; // (L, 3*C)
    float* attprojw; // (L, C, C)
    float* attprojb; // (L, C)
    float* ln2w; // (L, C)
    float* ln2b; // (L, C)
    float* fcw; // (L, 4*C, C)
    float* fcb; // (L, 4*C)
    float* fcprojw; // (L, C, 4*C)
    float* fcprojb; // (L, C)
    float* lnfw; // (C)
    float* lnfb; // (C)
} ParameterTensors;

// allocate memory for the parameters and point the individual tensors to the right places
float* malloc_and_point_parameters(ParameterTensors* params, size_t* param_sizes) {
    size_t num_parameters = 0;
    for (size_t i = 0; i < NUM_PARAMETER_TENSORS; i++) {
        num_parameters += param_sizes[i];
    }
    // malloc all parameters all at once
    float* params_memory = (float*)malloc(num_parameters * sizeof(float));
    // assign all the tensors
    float** ptrs[] = {
        &params->wte, &params->wpe, &params->ln1w, &params->ln1b, &params->qkvw, &params->qkvb,
        &params->attprojw, &params->attprojb, &params->ln2w, &params->ln2b, &params->fcw, &params->fcb,
        &params->fcprojw, &params->fcprojb, &params->lnfw, &params->lnfb
    };
    float* params_memory_iterator = params_memory;
    for (size_t i = 0; i < NUM_PARAMETER_TENSORS; i++) {
        *(ptrs[i]) = params_memory_iterator;
        params_memory_iterator += param_sizes[i];
    }
    return params_memory;
}

#define NUM_ACTIVATION_TENSORS 23
typedef struct {
    float* encoded; // (B, T, C)
    float* ln1; // (L, B, T, C)
    float* ln1_mean; // (L, B, T)
    float* ln1_rstd; // (L, B, T)
    float* qkv; // (L, B, T, 3*C)
    float* atty; // (L, B, T, C)
    float* preatt; // (L, B, NH, T, T)
    float* att; // (L, B, NH, T, T)
    float* attproj; // (L, B, T, C)
    float* residual2; // (L, B, T, C)
    float* ln2; // (L, B, T, C)
    float* ln2_mean; // (L, B, T)
    float* ln2_rstd; // (L, B, T)
    float* fch; // (L, B, T, 4*C)
    float* fch_gelu; // (L, B, T, 4*C)
    float* fcproj; // (L, B, T, C)
    float* residual3; // (L, B, T, C)
    float* lnf; // (B, T, C)
    float* lnf_mean; // (B, T)
    float* lnf_rstd; // (B, T)
    float* logits; // (B, T, V)
    float* probs; // (B, T, V)
    float* losses; // (B, T)
} ActivationTensors;

float* malloc_and_point_activations(ActivationTensors* acts, size_t* act_sizes) {
    size_t num_activations = 0;
    for (size_t i = 0; i < NUM_ACTIVATION_TENSORS; i++) {
        num_activations += act_sizes[i];
    }
    float* acts_memory = (float*)malloc(num_activations * sizeof(float));
    float** ptrs[] = {
        &acts->encoded, &acts->ln1, &acts->ln1_mean, &acts->ln1_rstd, &acts->qkv, &acts->atty,
        &acts->preatt, &acts->att, &acts->attproj, &acts->residual2, &acts->ln2, &acts->ln2_mean,
        &acts->ln2_rstd, &acts->fch, &acts->fch_gelu, &acts->fcproj, &acts->residual3, &acts->lnf,
        &acts->lnf_mean, &acts->lnf_rstd, &acts->logits, &acts->probs, &acts->losses
    };
    float* acts_memory_iterator = acts_memory;
    for (size_t i = 0; i < NUM_ACTIVATION_TENSORS; i++) {
        *(ptrs[i]) = acts_memory_iterator;
        acts_memory_iterator += act_sizes[i];
    }
    return acts_memory;
}

typedef struct {
    int max_seq_len; // max sequence length, e.g. 1024
    int vocab_size; // vocab size, e.g. 50257
    int num_layers; // number of layers, e.g. 12
    int num_heads; // number of heads in attention, e.g. 12
    int channels; // number of channels, e.g. 768
} GPT2Config;

typedef struct {
    GPT2Config config;
    // the weights (parameters) of the model, and their sizes
    ParameterTensors params;
    size_t param_sizes[NUM_PARAMETER_TENSORS];
    float* params_memory;
    int num_parameters;
    // gradients of the weights
    ParameterTensors grads;
    float* grads_memory;
    // buffers for the AdamW optimizer
    float* m_memory;
    float* v_memory;
    // the activations of the model, and their sizes
    ActivationTensors acts;
    size_t act_sizes[NUM_ACTIVATION_TENSORS];
    float* acts_memory;
    int num_activations;
    // gradients of the activations
    ActivationTensors grads_acts;
    float* grads_acts_memory;
    // other run state configuration
    int batch_size; // the batch size (B) of current forward pass
    int seq_len; // the sequence length (T) of current forward pass
    int* inputs; // the input tokens for the current forward pass
    int* targets; // the target tokens for the current forward pass
    float mean_loss; // after a forward pass with targets, will be populated with the mean loss
} GPT2;

void gpt2_build_from_checkpoint(GPT2 *model, char* checkpoint_path) {

    // read in model from a checkpoint file
    FILE *model_file = fopen(checkpoint_path, "rb");
    if (model_file == NULL) { printf("Error opening model file\n"); exit(1); }
    int model_header[256];
    fread(model_header, sizeof(int), 256, model_file);
    if (model_header[0] != 20240326) { printf("Bad magic model file"); exit(1); }
    if (model_header[1] != 1) { printf("Bad version in model file"); exit(1); }

    // read in hyperparameters
    int maxT, V, L, NH, C;
    model->config.max_seq_len = maxT = model_header[2];
    model->config.vocab_size = V = model_header[3];
    model->config.num_layers = L = model_header[4];
    model->config.num_heads = NH = model_header[5];
    model->config.channels = C = model_header[6];

    // allocate space for all the parameters and read them in
    model->param_sizes[0] = V * C; // wte
    model->param_sizes[1] = maxT * C; // wpe
    model->param_sizes[2] = L * C; // ln1w
    model->param_sizes[3] = L * C; // ln1b
    model->param_sizes[4] = L * (3 * C) * C; // qkvw
    model->param_sizes[5] = L * (3 * C); // qkvb
    model->param_sizes[6] = L * C * C; // attprojw
    model->param_sizes[7] = L * C; // attprojb
    model->param_sizes[8] = L * C; // ln2w
    model->param_sizes[9] = L * C; // ln2b
    model->param_sizes[10] = L * (4 * C) * C; // fcw
    model->param_sizes[11] = L * (4 * C); // fcb
    model->param_sizes[12] = L * C * (4 * C); // fcprojw
    model->param_sizes[13] = L * C; // fcprojb
    model->param_sizes[14] = C; // lnfw
    model->param_sizes[15] = C; // lnfb

    // cound the number of paramaters
    size_t num_parameters = 0;
    for (size_t i = 0; i < NUM_PARAMETER_TENSORS; i++) {
        num_parameters += model->param_sizes[i];
    }
    model->num_parameters = num_parameters;

    // read in all the parameters from file
    model->params_memory = malloc_and_point_parameters(&model->params, model->param_sizes);
    fread(model->params_memory, sizeof(float), num_parameters, model_file);
    fclose(model_file);

    // other inits
    model->acts_memory = NULL;
    model->grads_memory = NULL;
    model->m_memory = NULL;
    model->v_memory = NULL;
    model->grads_acts_memory = NULL;
    model->inputs = NULL;
    model->targets = NULL;
    model->batch_size = 0;
    model->seq_len = 0;
    model->mean_loss = -1.0f; // -1.0f will designate no loss
}

void gpt2_forward(GPT2 *model, int* inputs, int B, int T) {
    // convenience parameters
    int V = model->config.vocab_size;
    int L = model->config.num_layers;
    int NH = model->config.num_heads;
    int C = model->config.channels;

    // record the current B,T as well
    model->batch_size = B;
    model->seq_len = T;
    // and now allocate the space
    model->act_sizes[0] = B * T * C; // encoded
    model->act_sizes[1] = L * B * T * C; // ln1
    model->act_sizes[2] = L * B * T;  // ln1_mean
    model->act_sizes[3] = L * B * T;  // ln1_rstd
    model->act_sizes[4] = L * B * T * 3*C; // qkv
    model->act_sizes[5] = L * B * T * C;  // atty
    model->act_sizes[6] = L * B * NH * T * T;  // preatt
    model->act_sizes[7] = L * B * NH * T * T;  // att
    model->act_sizes[8] = L * B * T * C; // attproj
    model->act_sizes[9] = L * B * T * C; // residual2
    model->act_sizes[10] = L * B * T * C; // ln2
    model->act_sizes[11] = L * B * T; // ln2_mean
    model->act_sizes[12] = L * B * T; // ln2_rstd
    model->act_sizes[13] = L * B * T * 4*C; // fch
    model->act_sizes[14] = L * B * T * 4*C; // fch_gelu
    model->act_sizes[15] = L * B * T * C; // fcproj
    model->act_sizes[16] = L * B * T * C; // residual3
    model->act_sizes[17] = B * T * C; // lnf
    model->act_sizes[18] = B * T; // lnf_mean
    model->act_sizes[19] = B * T; // lnf_rstd
    model->act_sizes[20] = B * T * V; // logits
    model->act_sizes[21] = B * T * V; // probs
    model->act_sizes[22] = B * T; // losses
    size_t num_activations = 0;
    for (size_t i = 0; i < NUM_ACTIVATION_TENSORS; i++) {
        num_activations += model->act_sizes[i];
    }
    model->num_activations = num_activations;

    if (model->acts_memory) {
        free(model->acts_memory);
        model->acts_memory = NULL;
    }
    model->acts_memory = malloc_and_point_activations(&model->acts, model->act_sizes);

    // also create memory for caching inputs and targets
    if (model->inputs) {
        free(model->inputs);
    }
    model->inputs = (int*)malloc(B * T * sizeof(int));

    // cache the inputs/targets
    memcpy(model->inputs, inputs, B * T * sizeof(int));

    // forward pass
    ParameterTensors params = model->params; // for brevity
    ActivationTensors acts = model->acts;
    float* residual;
    encoder_forward(acts.encoded, inputs, params.wte, params.wpe, B, T, C, model->config.num_threads); // encoding goes into residual[0]
    for (int l = 0; l < L; l++) {

        residual = l == 0 ? acts.encoded : acts.residual3 + (l-1) * B * T * C;

        // get the pointers of the weights for this layer
        float* l_ln1w = params.ln1w + l * C;
        float* l_ln1b = params.ln1b + l * C;
        float* l_qkvw = params.qkvw + l * 3*C * C;
        float* l_qkvb = params.qkvb + l * 3*C;
        float* l_attprojw = params.attprojw + l * C * C;
        float* l_attprojb = params.attprojb + l * C;
        float* l_ln2w = params.ln2w + l * C;
        float* l_ln2b = params.ln2b + l * C;
        float* l_fcw = params.fcw + l * 4*C * C;
        float* l_fcb = params.fcb + l * 4*C;
        float* l_fcprojw = params.fcprojw + l * C * 4*C;
        float* l_fcprojb = params.fcprojb + l * C;

        // get the pointers of the activations for this layer
        float* l_ln1 = acts.ln1 + l * B * T * C;
        float* l_ln1_mean = acts.ln1_mean + l * B * T;
        float* l_ln1_rstd = acts.ln1_rstd + l * B * T;
        float* l_qkv = acts.qkv + l * B * T * 3*C;
        float* l_atty = acts.atty + l * B * T * C;
        float* l_preatt = acts.preatt + l * B * NH * T * T;
        float* l_att = acts.att + l * B * NH * T * T;
        float* l_attproj = acts.attproj + l * B * T * C;
        float* l_residual2 = acts.residual2 + l * B * T * C;
        float* l_ln2 = acts.ln2 + l * B * T * C;
        float* l_ln2_mean = acts.ln2_mean + l * B * T;
        float* l_ln2_rstd = acts.ln2_rstd + l * B * T;
        float* l_fch = acts.fch + l * B * T * 4*C;
        float* l_fch_gelu = acts.fch_gelu + l * B * T * 4*C;
        float* l_fcproj = acts.fcproj + l * B * T * C;
        float* l_residual3 = acts.residual3 + l * B * T * C;

        // now do the forward pass
        layernorm_forward(l_ln1, l_ln1_mean, l_ln1_rstd, residual, l_ln1w, l_ln1b, B, T, C, model->config.num_threads);
        matmul_forward(l_qkv, l_ln1, l_qkvw, l_qkvb, B, T, C, 3*C, model->config.num_threads);
        attention_forward(l_atty, l_preatt, l_att, l_qkv, B, T, C, NH, model->config.num_threads);
        matmul_forward(l_attproj, l_atty, l_attprojw, l_attprojb, B, T, C, C, model->config.num_threads);
        residual_forward(l_residual2, residual, l_attproj, B*T*C, model->config.num_threads);
        layernorm_forward(l_ln2, l_ln2_mean, l_ln2_rstd, l_residual2, l_ln2w, l_ln2b, B, T, C, model->config.num_threads);
        matmul_forward(l_fch, l_ln2, l_fcw, l_fcb, B, T, C, 4*C, model->config.num_threads);
        gelu_forward(l_fch_gelu, l_fch, B*T*4*C, model->config.num_threads);
        matmul_forward(l_fcproj, l_fch_gelu, l_fcprojw, l_fcprojb, B, T, 4*C, C, model->config.num_threads);
        residual_forward(l_residual3, l_residual2, l_fcproj, B*T*C, model->config.num_threads);
    }
    residual = acts.residual3 + (L-1) * B * T * C; // last residual is in residual3
    layernorm_forward(acts.lnf, acts.lnf_mean, acts.lnf_rstd, residual, params.lnfw, params.lnfb, B, T, C, model->config.num_threads);
    matmul_forward(acts.logits, acts.lnf, params.wte, NULL, B, T, C, V, model->config.num_threads);
    softmax_forward(acts.probs, acts.logits, B, T, V, model->config.num_threads);
}


void gpt2_zero_grad(GPT2 *model) {
    if(model->grads_memory != NULL) { memset(model->grads_memory, 0, model->num_parameters * sizeof(float)); }
    if(model->grads_acts_memory != NULL) { memset(model->grads_acts_memory, 0, model->num_activations * sizeof(float)); }
}

void gpt2_free(GPT2 *model) {
    free(model->params_memory);
    free(model->grads_memory);
    free(model->m_memory);
    free(model->v_memory);
    free(model->acts_memory);
    free(model->grads_acts_memory);
    free(model->inputs);
    free(model->targets);
}

int sample_mult(float* probabilities, int n) {
    // sample index from probabilities (they must sum to 1!)
    // coin can be a random number in [0, 1), usually from random_f32()
    float cdf = 0.0f, coin = 0.5f;
    for (int i = 0; i < n; i++) {
        cdf += probabilities[i];
        if (coin < cdf) {
            return i;
        }
    }
    return n - 1; // in case of rounding errors
}

// the GPT-2 end-of-text token id
#define GPT2_EOT 50256

int main(int argc, char** argv) {
    GPT2 model;
    gpt2_build_from_checkpoint(&model, "gpt2_124M.bin");
    const int n = 10;  // Token limit.

    if (argc == 1) {
        printf("Provide at least one token.\n");
        exit(1);
    }
    if (argc > n) {
        printf("Tow many tokens.\n");
        exit(1);
    }

    int tokens[n];

    for (int i = 0; i < n; i++) {
        if (i + 1 < argc) {
            tokens[i] = strtol(argv[i + 1], NULL, 10);
        } else {
            tokens[i] = GPT2_EOT;
        }
    }

    for (int t = argc - 1; t < n; t++) {
        gpt2_forward(&model, tokens, 1, t);
        float* probs = model.acts.probs + (t-1) * model.config.vocab_size;
        int next_token = sample_mult(probs, model.config.vocab_size);
        tokens[t] = next_token;

        printf("%d\n", tokens[t]);
        fflush(stdout);
    }

    gpt2_free(&model);

    return 0;
}
