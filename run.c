/* Inference for Llama-2 Transformer model in pure C 
   The Llama 2 Everywhere @trholding (Vulcan) fork   */
  
#include "assert.h"

#define memcpy __builtin_memcpy

// ----------------------------------------------------------------------------
// L2E Humanoid : Linux Kernel Support Directives
// 

#define _DEFTOSTR(LSTR) #LSTR
#define DEFTOSTR(LSTR) _DEFTOSTR(LSTR)

#define LOOPSTATUS 0 // Status off

#ifndef LINUXK
#define OSPROMPT L2E$
#endif

#ifdef LINUXK
#define INC_BIN 
#define LLOOP
#define LOOPSTATUS 1 // Status on
#endif
   
// ----------------------------------------------------------------------------
// L2E Asteroid : Unikraft Unikernel Support Directives
// 

#ifdef UNIK
#define STRLIT 
#define LLOOP
#define LOOPSTATUS 1 // Status on
#endif

// ----------------------------------------------------------------------------
// INCBIN Embedding Support Directives
// https://github.com/graphitemaster/incbin

// String substitution macro needed to pass paths to INCBIN
#define ADDPATH(FPATH) TOSTR(FPATH)
#define TOSTR(FPATH) #FPATH

#ifdef INC_BIN // Support for embedding model and tokenizer

#define INCBIN_PREFIX emb_
#define INCBIN_STYLE INCBIN_STYLE_SNAKE
#include "incbin.h"

#ifndef MODPATH
#define MODPATH out/model.bin // default model path
#endif
#ifndef TOKPATH
#define TOKPATH tokenizer.bin // default tokenizer path
#endif

INCBIN(Model, ADDPATH(MODPATH)); // Model path is passed via makefile
INCBIN(Tokenizer, ADDPATH(TOKPATH)); // Tokenizer path is passed via makefile

#endif

// ----------------------------------------------------------------------------
// strliteral (STRLIT) Embedding Support Directives
// https://github.com/mortie/strliteral

#ifdef STRLIT
#include "model.h"
#include "tokenizer.h"
#endif

// ----------------------------------------------------------------------------
// Actually Portable Executable Format Preprocessor Directives

#ifdef COSMO_BLINK // Support ARM 64 Bit via Blink VM Emulation
__static_yoink("blink_linux_aarch64");  // for raspberry pi
__static_yoink("blink_xnu_aarch64");    // is apple silicon
#endif

#ifdef COSMO_METAL // Support VGA Console when running bare metal
__static_yoink("vga_console");
#endif

#ifdef COSMO_ZIP // Support embedded models via Zip Archive support
__static_yoink("zipos");
#endif

// ----------------------------------------------------------------------------
// BLAS Support

#if defined(CLBLAST) || defined(OPENBLAS) || defined(CBLAS) || defined(BLIS) || defined(MKL) || defined(ARMPL) || defined(AAF)
#define BLAS
#endif

#ifdef CLBLAST
#include <clblast_netlib_c.h>
#elif defined(BLIS)
#include "blis.h"
#include "cblas.h"
#elif defined(MKL)
#include "mkl.h"
#elif defined(ARMPL)
#include <armpl.h>
#elif defined(AAF)
#include <Accelerate/Accelerate.h>
#elif defined(OPENBLAS)
#include "cblas.h"
#elif defined(CBLAS)
#include <cblas.h>
#endif

// ----------------------------------------------------------------------------
// OpenMP and OpenACC Support

// Macro that makes a pragma enabled with string substitution
#define MKPRAGMA_(x) _Pragma (#x)
#define MK_PRAGMA(x) MKPRAGMA_(x)

// Portable OpenMP and OpenACC pragma macros
#ifdef OPENMP
#define ACCEL(VAR) MK_PRAGMA(omp parallel for private(VAR))
#elif defined(OPENACC)
#define ACCEL(VAR) MK_PRAGMA(acc parallel loop private(VAR))
#endif

// ----------------------------------------------------------------------------
// Standard Headers

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <math.h>
#include <string.h>
#include <fcntl.h>
#if defined _WIN32
    #include "win.h"
#else
    #include <unistd.h>
    #include <sys/mman.h>
#endif
// ----------------------------------------------------------------------------
// Transformer model

typedef struct {
    int dim; // transformer dimension
    int hidden_dim; // for ffn layers
    int n_layers; // number of layers
    int n_heads; // number of query heads
    int n_kv_heads; // number of key/value heads (can be < query heads because of multiquery)
    int vocab_size; // vocabulary size, usually 256 (byte-level)
    int seq_len; // max sequence length
} Config;

typedef struct {
    // token embedding table
    float* token_embedding_table;    // (vocab_size, dim)
    // weights for rmsnorms
    float* rms_att_weight; // (layer, dim) rmsnorm weights
    float* rms_ffn_weight; // (layer, dim)
    // weights for matmuls. note dim == n_heads * head_size
    float* wq; // (layer, dim, n_heads * head_size)
    float* wk; // (layer, dim, n_kv_heads * head_size)
    float* wv; // (layer, dim, n_kv_heads * head_size)
    float* wo; // (layer, n_heads * head_size, dim)
    // weights for ffn
    float* w1; // (layer, hidden_dim, dim)
    float* w2; // (layer, dim, hidden_dim)
    float* w3; // (layer, hidden_dim, dim)
    // final rmsnorm
    float* rms_final_weight; // (dim,)
    // (optional) classifier weights for the logits, on the last layer
    float* wcls;
} TransformerWeights;

typedef struct {
    // current wave of activations
    float *x; // activation at current time stamp (dim,)
    float *xb; // same, but inside a residual branch (dim,)
    float *xb2; // an additional buffer just for convenience (dim,)
    float *hb; // buffer for hidden dimension in the ffn (hidden_dim,)
    float *hb2; // buffer for hidden dimension in the ffn (hidden_dim,)
    float *q; // query (dim,)
    float *k; // key (dim,)
    float *v; // value (dim,)
    float *att; // buffer for scores/attention values (n_heads, seq_len)
    float *logits; // output logits
    // kv cache
    float* key_cache;   // (layer, seq_len, dim)
    float* value_cache; // (layer, seq_len, dim)
} RunState;

typedef struct {
    Config config; // the hyperparameters of the architecture (the blueprint)
    TransformerWeights weights; // the weights of the model
    TransformerWeights dweights;
    RunState state; // buffers for the "wave" of activations in the forward pass
    RunState dstate;
    // some more state needed to properly clean up the memory mapping (sigh)
    int fd; // file descriptor for memory mapping
    float* data; // memory mapped data pointer
    float* ddata;
    float* weights_ptr;
    float* dweights_ptr;
    ssize_t file_size; // size of the checkpoint file in bytes
    ssize_t dfile_size; // size of the checkpoint train file in bytes
} Transformer;

void malloc_run_state(RunState* s, Config* p) {
    // we calloc instead of malloc to keep valgrind happy
    int kv_dim = (p->dim * p->n_kv_heads) / p->n_heads;
    s->x = calloc(p->dim, sizeof(float));
    s->xb = calloc(p->dim, sizeof(float));
    s->xb2 = calloc(p->dim, sizeof(float));
    s->hb = calloc(p->hidden_dim, sizeof(float));
    s->hb2 = calloc(p->hidden_dim, sizeof(float));
    s->q = calloc(p->dim, sizeof(float));
    s->k = calloc(kv_dim, sizeof(float));
    s->v = calloc(kv_dim, sizeof(float));
    s->att = calloc(p->n_heads * p->seq_len, sizeof(float));
    s->logits = calloc(p->vocab_size, sizeof(float));
    s->key_cache = calloc(p->n_layers * p->seq_len * kv_dim, sizeof(float));
    s->value_cache = calloc(p->n_layers * p->seq_len * kv_dim, sizeof(float));
    // ensure all mallocs went fine
    if (!s->x || !s->xb || !s->xb2 || !s->hb || !s->hb2 || !s->q
     || !s->k || !s->v || !s->att || !s->logits || !s->key_cache
     || !s->value_cache) {
        fprintf(stderr, "malloc failed!\n");
        exit(EXIT_FAILURE);
    }
}

void zero_run_state(RunState* s, Config* p) {
    // we calloc instead of malloc to keep valgrind happy
    memset(s->x, 0, p->dim * sizeof(float));
    memset(s->xb, 0, p->dim * sizeof(float));
    memset(s->xb2, 0, p->dim * sizeof(float));
    memset(s->hb, 0,p->hidden_dim * sizeof(float));
    memset(s->hb2, 0,p->hidden_dim * sizeof(float));
    memset(s->q, 0,p->dim * sizeof(float));
    memset(s->k, 0,p->dim * sizeof(float));
    memset(s->v, 0,p->dim * sizeof(float));
    memset(s->att, 0,p->n_heads * p->seq_len * sizeof(float));
    memset(s->logits, 0,p->vocab_size * sizeof(float));
    memset(s->key_cache, 0,p->n_layers * p->seq_len * p->dim * sizeof(float));
    memset(s->value_cache, 0,p->n_layers * p->seq_len * p->dim * sizeof(float));
}

void free_run_state(RunState* s) {
    free(s->x);
    free(s->xb);
    free(s->xb2);
    free(s->hb);
    free(s->hb2);
    free(s->q);
    free(s->k);
    free(s->v);
    free(s->att);
    free(s->logits);
    free(s->key_cache);
    free(s->value_cache);
}

void memory_map_weights(TransformerWeights *w, Config* p, float* ptr, int shared_weights) {
    int head_size = p->dim / p->n_heads;
    // make sure the multiplications below are done in 64bit to fit the parameter counts of 13B+ models
    unsigned long long n_layers = p->n_layers;
    w->token_embedding_table = ptr;
    ptr += p->vocab_size * p->dim;
    w->rms_att_weight = ptr;
    ptr += n_layers * p->dim;
    w->wq = ptr;
    ptr += n_layers * p->dim * (p->n_heads * head_size);
    w->wk = ptr;
    ptr += n_layers * p->dim * (p->n_kv_heads * head_size);
    w->wv = ptr;
    ptr += n_layers * p->dim * (p->n_kv_heads * head_size);
    w->wo = ptr;
    ptr += n_layers * (p->n_heads * head_size) * p->dim;
    w->rms_ffn_weight = ptr;
    ptr += n_layers * p->dim;
    w->w1 = ptr;
    ptr += n_layers * p->dim * p->hidden_dim;
    w->w2 = ptr;
    ptr += n_layers * p->hidden_dim * p->dim;
    w->w3 = ptr;
    ptr += n_layers * p->dim * p->hidden_dim;
    w->rms_final_weight = ptr;
    ptr += p->dim;
    ptr += p->seq_len * head_size / 2; // skip what used to be freq_cis_real (for RoPE)
    ptr += p->seq_len * head_size / 2; // skip what used to be freq_cis_imag (for RoPE)
    w->wcls = shared_weights ? w->token_embedding_table : ptr;
}


#if defined (INC_BIN) || defined(STRLIT)
void read_checkpoint(char* checkpoint, Config* config, TransformerWeights* weights, 
                     int* fd, float** data, ssize_t* file_size) {
    // read config header directly from the checkpoint data
    memcpy(config, checkpoint, sizeof(Config));
    int shared_weights = config->vocab_size > 0 ? 1 : 0;
    config->vocab_size = abs(config->vocab_size);
    *file_size = strlen(checkpoint); // get the data size, in bytes
    // memory map the Transformer weights
    *data = (float*)(checkpoint + sizeof(Config));    
    float* weights_ptr = *data;
    *fd = -1;
    memory_map_weights(weights, config, weights_ptr, shared_weights);
}
#else
void read_checkpoint(char* checkpoint, Config* config, TransformerWeights* weights, TransformerWeights* dweights,
		     float** weights_ptr, float** const dweights_ptr,
                     int* fd, float** data, float** ddata, ssize_t* file_size, ssize_t* dfile_size) {
    FILE *file = fopen(checkpoint, "rb");
    if (!file) { fprintf(stderr, "Couldn't open file %s\n", checkpoint); exit(EXIT_FAILURE); }
    // read in the config header
    if (fread(config, sizeof(Config), 1, file) != 1) { exit(EXIT_FAILURE); }
    // negative vocab size is hacky way of signaling unshared weights. bit yikes.
    int shared_weights = config->vocab_size > 0 ? 1 : 0;
    config->vocab_size = abs(config->vocab_size);
    // figure out the file size
    fseek(file, 0, SEEK_END); // move file pointer to end of file
    *file_size = ftell(file); // get the file size, in bytes
    fclose(file);
    // memory map the Transformer weights into the data pointer
    *fd = open(checkpoint, O_RDONLY); // open in read only mode
    if (*fd == -1) { fprintf(stderr, "open failed!\n"); exit(EXIT_FAILURE); }
    *data = mmap(NULL, *file_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, *fd, 0);
    if (*data == MAP_FAILED) { fprintf(stderr, "mmap data failed!\n"); exit(EXIT_FAILURE); }
    *weights_ptr = *data + sizeof(Config)/sizeof(float);
    memory_map_weights(weights, config, *weights_ptr, shared_weights);
#if AD
    *ddata = mmap(NULL, *file_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (*ddata == MAP_FAILED) { printf("mmap ddata failed!\n"); exit(EXIT_FAILURE); }
    memset(*ddata, 0, *file_size);
    *dweights_ptr = *ddata + sizeof(Config)/sizeof(float);    
    memory_map_weights(dweights, config, *dweights_ptr, shared_weights);
#endif
}
#endif

void build_transformer(Transformer *t, char* checkpoint_path) {
    // read in the Config and the Weights from the checkpoint
    read_checkpoint(checkpoint_path, &t->config, &t->weights, &t->dweights, &t->weights_ptr, &t->dweights_ptr, &t->fd, &t->data, &t->ddata, &t->file_size, &t->dfile_size);
    // allocate the RunState buffers
    malloc_run_state(&t->state, &t->config);
    // new, Manuel
#if AD
    malloc_run_state(&t->dstate, &t->config);
#endif
}

void free_transformer(Transformer* t) {
    // close the memory mapping
    if (t->data != MAP_FAILED) { munmap(t->data, t->file_size); }
    if (t->ddata != MAP_FAILED) { munmap(t->ddata, t->file_size); }
    if (t->fd != -1) { close(t->fd); }
    // free the RunState buffers
    free_run_state(&t->state);
#if AD
    free_run_state(&t->dstate);
#endif
}

// ----------------------------------------------------------------------------
// neural net blocks; the dynamics of the Transformer

static inline void rmsnorm(float* __restrict__ o, float* __restrict__ x, float* __restrict__ weight, int size) {
    // calculate sum of squares
    float ss = 0.0f;
    #ifdef BLAS
    ss = cblas_sdot(size, x, 1.0f, x, 1.0f);
    #else
    for (int j = 0; j < size; j++) {
        ss += x[j] * x[j];
    }
    #endif
    ss /= size;
    ss += 1e-5f;
    ss = 1.0f / sqrtf(ss);
    // normalize and scale
    for (int j = 0; j < size; j++) {
        o[j] = weight[j] * (ss * x[j]);
    }
}

static inline void softmax(float* x, int size) {
    // find max value (for numerical stability)
    float max_val = x[0];
    for (int i = 1; i < size; i++) {
        if (x[i] > max_val) {
            max_val = x[i];
        }
    }
    // exp and sum
    float sum = 0.0f;
    for (int i = 0; i < size; i++) {
        x[i] = expf(x[i] - max_val);
        sum += x[i];
    }

    // normalize
    #ifdef BLAS
    cblas_sscal(size, 1/sum, x, 1);
    #else
    for (int i = 0; i < size; i++) {
        x[i] /= sum;
    }
    #endif
}

#if 0
void
cblas_sgemv (const enum CBLAS_ORDER order, const enum CBLAS_TRANSPOSE TransA,
             const int M, const int N, const float alpha, const float *A,
             const int lda, const float *X, const int incX, const float beta,
             float *Y, const int incY)
{
#define BASE float
{
#define INDEX int
#define OFFSET(N, incX) ((incX) > 0 ?  0 : ((N) - 1) * (-(incX)))
  INDEX i, j;
  INDEX lenX, lenY;

  const int Trans = (TransA != CblasConjTrans) ? TransA : CblasTrans;

 //  CHECK_ARGS12(GEMV,order,TransA,M,N,alpha,A,lda,X,incX,beta,Y,incY);

  if (M == 0 || N == 0)
    return;

  if (alpha == 0.0 && beta == 1.0)
    return;

  if (Trans == CblasNoTrans) {
    lenX = N;
    lenY = M;
  } else {
    lenX = M;
    lenY = N;
  }

  /* form  y := beta*y */
  if (beta == 0.0) {
    INDEX iy = OFFSET(lenY, incY);
    for (i = 0; i < lenY; i++) {
      Y[iy] = 0.0;
      iy += incY;
    }
  } else if (beta != 1.0) {
    INDEX iy = OFFSET(lenY, incY);
    for (i = 0; i < lenY; i++) {
      Y[iy] *= beta;
      iy += incY;
    }
  }

  if (alpha == 0.0)
    return;

  if ((order == CblasRowMajor && Trans == CblasNoTrans)
      || (order == CblasColMajor && Trans == CblasTrans)) {
    /* form  y := alpha*A*x + y */
    INDEX iy = OFFSET(lenY, incY);
    for (i = 0; i < lenY; i++) {
      BASE temp = 0.0;
      INDEX ix = OFFSET(lenX, incX);
      for (j = 0; j < lenX; j++) {
        temp += X[ix] * A[lda * i + j];
        ix += incX;
      }
      Y[iy] += alpha * temp;
      iy += incY;
    }
  } else if ((order == CblasRowMajor && Trans == CblasTrans)
             || (order == CblasColMajor && Trans == CblasNoTrans)) {
    /* form  y := alpha*A'*x + y */
    INDEX ix = OFFSET(lenX, incX);
    for (j = 0; j < lenX; j++) {
      const BASE temp = alpha * X[ix];
        INDEX iy = OFFSET(lenY, incY);
        for (i = 0; i < lenY; i++) {
          Y[iy] += temp * A[lda * j + i];
          iy += incY;
        }
      ix += incX;
    }
  } else {
    printf("unrecognized operation");
  }
}
#undef BASE
}
#endif

static inline void matmul(float* __restrict__ xout, float* __restrict__ x, float* __restrict__ w, int n, int d) {
    // W (d,n) @ x (n,) -> xout (d,)
    // by far the most amount of time is spent inside this little function
    #ifdef BLAS
    cblas_sgemv(CblasRowMajor, CblasNoTrans, d, n, 1.0f, w, n, x, 1, 0.0f, xout, 1);
    #else
    int i;
    #ifdef ACCEL
    ACCEL(i) // OMP/OACC Macro
    #endif
    for (i = 0; i < d; i++) {
        float val = 0.0f;
        for (int j = 0; j < n; j++) {
            val += w[i * n + j] * x[j];
        }
        xout[i] = val;
    }
    #endif
}

//float* forward(Transformer* transformer, int token, int pos) {
__attribute__((always_inline))
static inline float* forward(int token, int pos, Config *__restrict__ p, TransformerWeights *__restrict__ w, RunState *__restrict__ s) {

    // a few convenience variables
    //Config* p = &transformer->config;
    //TransformerWeights* w = &transformer->weights;
    //RunState* s = &transformer->state;
    float *x = s->x;
    int dim = p->dim;
    int kv_dim = (p->dim * p->n_kv_heads) / p->n_heads;
    int kv_mul = p->n_heads / p->n_kv_heads; // integer multiplier of the kv sharing in multiquery
    int hidden_dim =  p->hidden_dim;
    int head_size = dim / p->n_heads;
    
    // copy the token embedding into x
    float* content_row = w->token_embedding_table + token * dim;
    memcpy(x, content_row, dim*sizeof(*x));

    for(unsigned long long l = 0; l < p->n_layers; l++) {

        // attention rmsnorm
        rmsnorm(s->xb, x, w->rms_att_weight + l*dim, dim);

        // qkv matmuls for this position
        matmul(s->q, s->xb, w->wq + l*dim*dim, dim, dim);
        matmul(s->k, s->xb, w->wk + l*dim*kv_dim, dim, kv_dim);
        matmul(s->v, s->xb, w->wv + l*dim*kv_dim, dim, kv_dim);

        // RoPE relative positional encoding: complex-valued rotate q and k in each head
        for (int i = 0; i < dim; i+=2) {
            int head_dim = i % head_size;
            float freq = 1.0f / powf(10000.0f, head_dim / (float)head_size);
            float val = pos * freq;
            float fcr = cosf(val);
            float fci = sinf(val);
            int rotn = i < kv_dim ? 2 : 1; // how many vectors? 2 = q & k, 1 = q only
            for (int v = 0; v < rotn; v++) {
                float* vec = v == 0 ? s->q : s->k; // the vector to rotate (query or key)
                float v0 = vec[i];
                float v1 = vec[i+1];
                vec[i]   = v0 * fcr - v1 * fci;
                vec[i+1] = v0 * fci + v1 * fcr;
            }
        }

        // save key,value at this time step (pos) to our kv cache
        int loff = l * p->seq_len * kv_dim; // kv cache layer offset for convenience
        float* key_cache_row = s->key_cache + loff + pos * kv_dim;
        float* value_cache_row = s->value_cache + loff + pos * kv_dim;
        memcpy(key_cache_row, s->k, kv_dim * sizeof(*key_cache_row));
        memcpy(value_cache_row, s->v, kv_dim * sizeof(*value_cache_row));

        // multihead attention. iterate over all heads
        int h;
        #ifdef ACCEL
        ACCEL(h) // OMP/OACC Macro
        #endif
        for (h = 0; h < p->n_heads; h++) {
            // get the query vector for this head
            float* q = s->q + h * head_size;
            // attention scores for this head
            float* att = s->att + h * p->seq_len;
            // iterate over all timesteps, including the current one
            for (int t = 0; t <= pos; t++) {
                // get the key vector for this head and at this timestep
                float* k = s->key_cache + loff + t * kv_dim + (h / kv_mul) * head_size;
                // calculate the attention score as the dot product of q and k
                float score = 0.0f;
#ifdef BLAS
                score = cblas_sdot(head_size, q, 1, k, 1);
#else
                for (int i = 0; i < head_size; i++) {
                    score += q[i] * k[i];
                }
#endif
                score /= sqrtf(head_size);
                // save the score to the attention buffer
                att[t] = score;
            }

            // softmax the scores to get attention weights, from 0..pos inclusively
            softmax(att, pos + 1);

            // weighted sum of the values, store back into xb
            float* xb = s->xb + h * head_size;
            memset(xb, 0, head_size * sizeof(float));
            for (int t = 0; t <= pos; t++) {
                // get the value vector for this head and at this timestep
                float* v = s->value_cache + loff + t * kv_dim + (h / kv_mul) * head_size;
                // get the attention weight for this timestep
                float a = att[t];
                // accumulate the weighted value into xb
                for (int i = 0; i < head_size; i++) {
                    xb[i] += a * v[i];
                }
            }
        }

        // final matmul to get the output of the attention
        matmul(s->xb2, s->xb, w->wo + l*dim*dim, dim, dim);

        // residual connection back into x
        for (int i = 0; i < dim; i++) {
            x[i] += s->xb2[i];
        }

        // ffn rmsnorm
        rmsnorm(s->xb, x, w->rms_ffn_weight + l*dim, dim);

        // Now for FFN in PyTorch we have: self.w2(F.silu(self.w1(x)) * self.w3(x))
        // first calculate self.w1(x) and self.w3(x)
        matmul(s->hb, s->xb, w->w1 + l*dim*hidden_dim, dim, hidden_dim);
        matmul(s->hb2, s->xb, w->w3 + l*dim*hidden_dim, dim, hidden_dim);

        // F.silu; silu(x)=x*σ(x),where σ(x) is the logistic sigmoid
        for (int i = 0; i < hidden_dim; i++) {
            s->hb[i] = s->hb[i] * (1.0f / (1.0f + expf(-s->hb[i])));
        }

        // elementwise multiply with w3(x)
        for (int i = 0; i < hidden_dim; i++) {
            s->hb[i] = s->hb[i] * s->hb2[i];
        }

        // final matmul to get the output of the ffn
        matmul(s->xb, s->hb, w->w2 + l*dim*hidden_dim, hidden_dim, dim);

        // residual connection
        for (int i = 0; i < dim; i++) {
            x[i] += s->xb[i];
        }
    }

    // final rmsnorm
    rmsnorm(x, x, w->rms_final_weight, dim);

    // classifier into logits
    matmul(s->logits, x, w->wcls, p->dim, p->vocab_size);
    return s->logits;
}

// ----------------------------------------------------------------------------
// The Byte Pair Encoding (BPE) Tokenizer that translates strings <-> tokens

typedef struct {
    char** vocab;
    float* vocab_scores;
    int vocab_size;
    unsigned int max_token_length;
    char byte_piece[2];
} Tokenizer;


#if defined (INC_BIN) || defined(STRLIT)
void build_tokenizer(Tokenizer* t, char* tokenizer_path, int vocab_size) {
    t->vocab_size = vocab_size;
    t->vocab = (char**)malloc(vocab_size * sizeof(char*));
    t->vocab_scores = (float*)malloc(vocab_size * sizeof(float));
    t->byte_piece[1] = '\0'; // null terminate the byte_piece string
    // Parse the data from tokenizer_path
    char* token_data = tokenizer_path;
    int token_data_offset = 0;

    // Read the max_token_length from token_data
    memcpy(&t->max_token_length, token_data, sizeof(int));
    token_data_offset += sizeof(int);

    int len;
    for (int i = 0; i < vocab_size; i++) {
        // Read the vocab_scores from token_data
        memcpy(t->vocab_scores + i, token_data + token_data_offset, sizeof(float));
        token_data_offset += sizeof(float);

        // Read the length of the vocabulary token
        memcpy(&len, token_data + token_data_offset, sizeof(int));
        token_data_offset += sizeof(int);

        // Allocate memory for the vocabulary token and copy the data
        t->vocab[i] = (char*)malloc(len + 1);
        memcpy(t->vocab[i], token_data + token_data_offset, len);
        t->vocab[i][len] = '\0'; // add the string terminating token
        token_data_offset += len;
    }
}
#else
void build_tokenizer(Tokenizer* t, char* tokenizer_path, int vocab_size) {
    // i should have written the vocab_size into the tokenizer file... sigh
    t->vocab_size = vocab_size;
    // malloc space to hold the scores and the strings
    t->vocab = (char**)malloc(vocab_size * sizeof(char*));
    t->vocab_scores = (float*)malloc(vocab_size * sizeof(float));
    t->byte_piece[1] = '\0'; // null terminate the byte_piece string
    // read in the file
    FILE *file = fopen(tokenizer_path, "rb");
    if (!file) { fprintf(stderr, "couldn't load %s\n", tokenizer_path); exit(EXIT_FAILURE); }
    if (fread(&t->max_token_length, sizeof(int), 1, file) != 1) { fprintf(stderr, "failed read\n"); exit(EXIT_FAILURE); }
    int len;
    for (int i = 0; i < vocab_size; i++) {
        if (fread(t->vocab_scores + i, sizeof(float), 1, file) != 1) { fprintf(stderr, "failed read\n"); exit(EXIT_FAILURE);}
        if (fread(&len, sizeof(int), 1, file) != 1) { fprintf(stderr, "failed read\n"); exit(EXIT_FAILURE); }
        t->vocab[i] = (char *)malloc(len + 1);
        if (fread(t->vocab[i], len, 1, file) != 1) { fprintf(stderr, "failed read\n"); exit(EXIT_FAILURE); }
        t->vocab[i][len] = '\0'; // add the string terminating token
    }
    fclose(file);
}
#endif

void free_tokenizer(Tokenizer* t) {
    for (int i = 0; i < t->vocab_size; i++) { free(t->vocab[i]); }
    free(t->vocab);
    free(t->vocab_scores);
}

char* decode(Tokenizer* t, int prev_token, int token) {
    char *piece = t->vocab[token];
    // following BOS (1) token, sentencepiece decoder strips any leading whitespace (see PR #89)
    if (prev_token == 1 && piece[0] == ' ') { piece++; }
    // careful, some tokens designate raw bytes, and look like e.g. '<0x01>'
    unsigned char byte_val;
    if (sscanf(piece, "<0x%02hhX>", &byte_val) == 1) {
        // ok this token is a raw byte token, careful to only print printable chars or whitespace
        // some of the other bytes can be various control codes, backspace, etc. => skip
        if (isprint(byte_val) || isspace(byte_val)) {
            t->byte_piece[0] = byte_val;
            piece = &t->byte_piece[0];
        }
    }
    return piece;
}

typedef struct {
    char *str;
    int id;
} TokenIndex;

int compare_tokens(const void *a, const void *b) {
    return strcmp(((TokenIndex*)a)->str, ((TokenIndex*)b)->str);
}

int str_lookup(char *str, TokenIndex *sorted_vocab, int vocab_size) {
    // efficiently find the perfect match for str in vocab, return its index or -1 if not found
    TokenIndex tok = { .str = str }; // acts as the key to search for
    TokenIndex *res = bsearch(&tok, sorted_vocab, vocab_size, sizeof(TokenIndex), compare_tokens);
    return res != NULL ? res->id : -1;
}

void encode(Tokenizer* t, char *text, int *tokens, int *n_tokens) {
    // encode the string text (input) into an upper-bound preallocated tokens[] array

    // sort vocabulary
    TokenIndex *sorted_vocab = malloc(t->vocab_size * sizeof(TokenIndex));
    for (int i = 0; i < t->vocab_size; i++) {
        sorted_vocab[i].str = t->vocab[i];
        sorted_vocab[i].id = i;
    }
    qsort(sorted_vocab, t->vocab_size, sizeof(TokenIndex), compare_tokens);

    // create a temporary buffer that will store merge candidates of always two consecutive tokens
    char* str_buffer = malloc((t->max_token_length*2 +1 +2) * sizeof(char)); // *2 for concat, +1 for null terminator +2 for UTF8 (in case max_token_lenght is 1)
    size_t str_len = 0;

    // add_dummy_prefix is true by default
    tokens[0] = str_lookup(" ", sorted_vocab, t->vocab_size);
    *n_tokens = 1; // the number of tokens

    // Okay UTF-8 time. This will get messy. Here is the reference from Wikipedia:
    // Code point ↔ UTF-8 conversion
    // First code point	Last code point	Byte 1	Byte 2	Byte 3	Byte 4
    // U+0000	U+007F	    0xxxxxxx
    // U+0080	U+07FF	    110xxxxx	10xxxxxx
    // U+0800	U+FFFF	    1110xxxx	10xxxxxx	10xxxxxx
    // U+10000	U+10FFFF    11110xxx	10xxxxxx	10xxxxxx	10xxxxxx

    // process the raw (UTF-8) byte sequence of the input string
    for (char *c = text; *c != '\0'; c++) {

        // reset buffer if the current byte is ASCII or a leading byte
        // 0xC0 is 11000000, so (*c & 0xC0) keeps the first 2 bits and zeros the rest
        // 0x80 is 10000000
        // in UTF-8, all continuation bytes start with "10" in first two bits
        // so in English this is: "if this byte is not a continuation byte"
        if ((*c & 0xC0) != 0x80) {
            // this byte must be either a leading byte (11...) or an ASCII char (0x...)
            // => reset our location, as we're starting a new UTF-8 codepoint
            str_len = 0;
        }

        // append the current byte to the buffer
        str_buffer[str_len++] = *c; // ++ is post-increment, incremented after this line
        str_buffer[str_len] = '\0';

        // while the next character is a continuation byte, continue appending
        // but if there are too many of them, just stop to avoid overruning str_buffer size.
        if ((*(c+1) & 0xC0) == 0x80 && str_len < 4) {
            continue;
        }

        // ok c+1 is not a continuation byte, so we've read in a full codepoint
        int id = str_lookup(str_buffer, sorted_vocab, t->vocab_size);

        if (id != -1) {
            // we found this codepoint in vocab, add it as a token
            tokens[(*n_tokens)++] = id;
        } else {
            // byte_fallback encoding: just encode each byte as a token
            // +3 is here because the first 3 vocab elements are <unk>, <s>, </s>
            // so the individual bytes only start at index 3
            for (int i=0; i < str_len; i++) {
                tokens[(*n_tokens)++] = (unsigned char)str_buffer[i] + 3;
            }
        }
        str_len = 0; // protect against a sequence of stray UTF8 continuation bytes
    }

    // merge the best consecutive pair each iteration, according the scores in vocab_scores
    while (1) {
        float best_score = -1e10;
        int best_id = -1;
        int best_idx = -1;

        for (int i=0; i < (*n_tokens-1); i++) {
            // check if we can merge the pair (tokens[i], tokens[i+1])
            sprintf(str_buffer, "%s%s", t->vocab[tokens[i]], t->vocab[tokens[i+1]]);
            int id = str_lookup(str_buffer, sorted_vocab, t->vocab_size);
            if (id != -1 && t->vocab_scores[id] > best_score) {
                // this merge pair exists in vocab! record its score and position
                best_score = t->vocab_scores[id];
                best_id = id;
                best_idx = i;
            }
        }

        if (best_idx == -1) {
            break; // we couldn't find any more pairs to merge, so we're done
        }

        // merge the consecutive pair (best_idx, best_idx+1) into new token best_id
        tokens[best_idx] = best_id;
        // delete token at position best_idx+1, shift the entire sequence back 1
        for (int i = best_idx+1; i < (*n_tokens-1); i++) {
            tokens[i] = tokens[i+1];
        }
        (*n_tokens)--; // token length decreased
    }

    free(str_buffer);
    free(sorted_vocab);
}

// ----------------------------------------------------------------------------
// The Sampler, which takes logits and returns a sampled token
// sampling can be done in a few ways: greedy argmax, sampling, top-p sampling

typedef struct {
    float prob;
    int index;
} ProbIndex; // struct used when sorting probabilities during top-p sampling

typedef struct {
    int vocab_size;
    ProbIndex* probindex; // buffer used in top-p sampling
} Sampler;

// rng should technically be a state variable of the Sampler
// leaving it global here for now for convenience, maybe move later
unsigned long long rng_seed;
unsigned int random_u32() {
    // xorshift rng: https://en.wikipedia.org/wiki/Xorshift#xorshift.2A
    rng_seed ^= rng_seed >> 12;
    rng_seed ^= rng_seed << 25;
    rng_seed ^= rng_seed >> 27;
    return (rng_seed * 0x2545F4914F6CDD1Dull) >> 32;
}
float random_f32() { // random float32 in [0,1)
    return (random_u32() >> 8) / 16777216.0f;
}

int sample_argmax(float* probabilities, int n) {
    // return the index that has the highest probability
    int max_i = 0;
    float max_p = probabilities[0];
    for (int i = 1; i < n; i++) {
        if (probabilities[i] > max_p) {
            max_i = i;
            max_p = probabilities[i];
        }
    }
    return max_i;
}

int sample_mult(float* probabilities, int n) {
    // sample index from probabilities (they must sum to 1!)
    float r = random_f32();
    float cdf = 0.0f;
    for (int i = 0; i < n; i++) {
        cdf += probabilities[i];
        if (r < cdf) {
            return i;
        }
    }
    return n - 1; // in case of rounding errors
}

int compare(const void* a, const void* b) {
    ProbIndex* a_ = (ProbIndex*) a;
    ProbIndex* b_ = (ProbIndex*) b;
    if (a_->prob > b_->prob) return -1;
    if (a_->prob < b_->prob) return 1;
    return 0;
}

int sample_topp(float* probabilities, int n, float topp, ProbIndex* probindex) {
    // top-p sampling (or "nucleus sampling") samples from the smallest set of
    // tokens that exceed probability topp. This way we never sample tokens that
    // have very low probabilities and are less likely to go "off the rails".

    int n0 = 0;
    // quicksort indices in descending order of probabilities
    // values smaller than (1 - topp) / (n - 1) cannot be part of the result
    // so for efficiency we crop these out as candidates before sorting
    const float cutoff = (1.0f - topp) / (n - 1);
    for (int i = 0; i < n; i++) {
        if (probabilities[i] >= cutoff) {
            probindex[n0].index = i;
            probindex[n0].prob = probabilities[i];
            n0++;
        }
    }
    qsort(probindex, n0, sizeof(ProbIndex), compare);

    // truncate the list where cumulative probability exceeds topp
    float cumulative_prob = 0.0f;
    int last_idx = n0 - 1; // in case of rounding errors consider all elements
    for (int i = 0; i < n0; i++) {
        cumulative_prob += probindex[i].prob;
        if (cumulative_prob > topp) {
            last_idx = i;
            break; // we've exceeded topp by including last_idx
        }
    }

    // sample from the truncated list
    float r = random_f32() * cumulative_prob;
    float cdf = 0.0f;
    for (int i = 0; i <= last_idx; i++) {
        cdf += probindex[i].prob;
        if (r < cdf) {
            return probindex[i].index;
        }
    }
    return probindex[last_idx].index; // in case of rounding errors
}

void build_sampler(Sampler* sampler, int vocab_size) {
    sampler->vocab_size = vocab_size;
    // probindex might not be needed, but it's a ~small buffer so we'll just malloc it
    sampler->probindex = malloc(vocab_size * sizeof(ProbIndex));
}

void free_sampler(Sampler* sampler) {
    free(sampler->probindex);
}

int sample(Sampler* sampler, float* logits, float temperature, float topp) {
    // sample the token given the logits and some hyperparameters
    int next;
    if (temperature == 0.0f) {
        // greedy argmax sampling: take the token with the highest probability
        next = sample_argmax(logits, sampler->vocab_size);
    } else {
        // apply the temperature to the logits
        for (int q=0; q<sampler->vocab_size; q++) { logits[q] /= temperature; }
        // apply softmax to the logits to get the probabilities for next token
        softmax(logits, sampler->vocab_size);
        // we sample from this distribution to get the next token
        if (topp <= 0 || topp >= 1) {
            // simply sample from the predicted probability distribution
            next = sample_mult(logits, sampler->vocab_size);
        } else {
            // top-p (nucleus) sampling, clamping the least likely tokens to zero
            next = sample_topp(logits, sampler->vocab_size, topp, sampler->probindex);
        }
    }
    return next;
}


float loss(int token, int pos, Config* __restrict__ config, RunState* __restrict__ s, TransformerWeights* __restrict__ w, int nexttok, float temperature) {
    float* logits = forward(token, pos, config, w, s);

    // apply the temperature to the logits
    for (int q=0; q<config->vocab_size; q++) { s->logits[q] /= temperature; }
    // printf("vocab_size: %d\n", config->vocab_size);

    // apply softmax to the logits to get the probabilities for next token
    //softmax(s->logits, config->vocab_size);
    softmax(logits, config->vocab_size);

    // printf("nexttok: %d\n", nexttok);
    // fflush(stdout);

    // assert(nexttok < config->vocab_size);
    // we now want to sample from this distribution to get the next token
    //next = sample(state.logits, config.vocab_size);
    // https://github.com/keras-team/keras/blob/21c25fd38023a3783950c5577383ffe51a62f650/keras/backend_config.py#L34
    //return -log(s->logits[nexttok] + 1e-7);
    float tmp = (nexttok == -1) ? 0 : logits[nexttok];
    return -log(tmp + 1e-7);
}

// ----------------------------------------------------------------------------
// utilities: time

long time_in_ms() {
    // return time in milliseconds, for benchmarking the model speed
    struct timespec time;
    clock_gettime(CLOCK_REALTIME, &time);
    return time.tv_sec * 1000 + time.tv_nsec / 1000000;
}

// ----------------------------------------------------------------------------
// LLama 2 Everywhere read prompt utility function

#if defined(COSMO_ZIP) || defined(INC_BIN) || defined(STRLIT)
void inprompt(char *lprompt) // Handle prompts
{
    fgets(lprompt, 1024, stdin);
    lprompt[strcspn(lprompt, "\n")] = '\0';
}
#endif

// ----------------------------------------------------------------------------
// int main

void error_usage() {
    fprintf(stderr, "Usage:   run <checkpoint> [options]\n");
    fprintf(stderr, "Example: run model.bin -n 256 -i \"Once upon a time\"\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -t <float>  temperature in [0,inf], default 1.0\n");
    fprintf(stderr, "  -p <float>  p value in top-p (nucleus) sampling in [0,1] default 0.9\n");
    fprintf(stderr, "  -s <int>    random seed, default time(NULL)\n");
    fprintf(stderr, "  -n <int>    number of steps to run for, default 256. 0 = max_seq_len\n");
    fprintf(stderr, "  -b <int>    number of tokens to buffer, default 1. 0 = max_seq_len\n");
    fprintf(stderr, "  -x <int>    extended info / stats, default 1 = on. 0 = off\n");    
    fprintf(stderr, "  -i <string> input prompt\n");
    fprintf(stderr, "  -z <string> optional path to custom tokenizer\n");
    fprintf(stderr, "  -e <string> optional path to training data\n");
    exit(EXIT_FAILURE);
}

int enzyme_const;
int enzyme_primal_return;
int enzyme_dup;
float __enzyme_autodiff(void*, 
        int,
        int, int,
        int, int,
        int, Config*,
        int, RunState*, RunState*,
        int, TransformerWeights*, TransformerWeights*,
        int,
        int, float);

int main(int argc, char *argv[]) {

    // default parameters
    char *checkpoint_path = NULL;  // e.g. out/model.bin
    char *tokenizer_path = "tokenizer.bin";
    float temperature = 1.0f; // 0.0 = greedy deterministic. 1.0 = original. don't set higher
    float topp = 0.9f;        // top-p in nucleus sampling. 1.0 = off. 0.9 works well, but slower
    rng_seed = 0; // seed rng with time by default
    int steps = 256;          // number of steps to run for
    char *prompt = NULL;      // prompt string
    int buffertokens = 1;     // output token buffer size
    int stats = 1;     // extended status info
    char *training_data = "trains.txt";
    
    
    #if defined(COSMO_ZIP) || defined(INC_BIN) || defined(STRLIT) // special case for embedded models
    // we read the embedded checkpoint from within the executable
    // 'checkpoint' is necessary arg
    #ifdef UNIK
    printf("\n*** GURU UNMEDITATION :: BOOT > LLAMA HAS AWAKENED ***\n\n");
    #endif
    #if defined(COSMO_ZIP)
    checkpoint_path = "/zip/out/model.bin";
    tokenizer_path = "/zip/tokenizer.bin";
    #elif defined(INC_BIN) || defined(STRLIT)
    checkpoint_path = emb_Model_data;
    tokenizer_path = emb_Tokenizer_data;
    #endif
    buffertokens=8;
    #ifdef LLOOP
    stats = LOOPSTATUS;
    while(1) { // start of loop
    #endif
    prompt=(char*)malloc(1024);
    printf("\n" DEFTOSTR(OSPROMPT)" ");
    fflush(stdout); 
    inprompt(prompt); // read prompt
    #else
    // poor man's C argparse so we can override the defaults above from the command line
    if (argc >= 2) { checkpoint_path = argv[1]; } else { error_usage(); }
    for (int i = 2; i < argc; i+=2) {
        // do some basic validation
        if (i + 1 >= argc) { error_usage(); } // must have arg after flag
        if (argv[i][0] != '-') { error_usage(); } // must start with dash
        if (strlen(argv[i]) != 2) { error_usage(); } // must be -x (one dash, one letter)
        // read in the args
        if (argv[i][1] == 't') { temperature = atof(argv[i + 1]); }
        else if (argv[i][1] == 'p') { topp = atof(argv[i + 1]); }
        else if (argv[i][1] == 's') { rng_seed = atoi(argv[i + 1]); }
        else if (argv[i][1] == 'n') { steps = atoi(argv[i + 1]); }
        else if (argv[i][1] == 'b') { buffertokens = atoi(argv[i + 1]); }
        else if (argv[i][1] == 'x') { stats = atoi(argv[i + 1]); }
        else if (argv[i][1] == 'i') { prompt = argv[i + 1]; }
        else if (argv[i][1] == 'z') { tokenizer_path = argv[i + 1]; }
        else if (argv[i][1] == 'e') { training_data = argv[i + 1]; } // Enzyme!
        else { error_usage(); }
    }
    #endif
    
    // parameter validation/overrides
    if (rng_seed <= 0) rng_seed = (unsigned int)time(NULL);
    if (temperature < 0.0) temperature = 0.0;
    if (topp < 0.0 || 1.0 < topp) topp = 0.9;
    if (steps <= 0) steps = 0;

    // build the Transformer via the model .bin file
    Transformer transformer;
    build_transformer(&transformer, checkpoint_path);

    // build the Tokenizer via the tokenizer .bin file
    Tokenizer tokenizer;
    build_tokenizer(&tokenizer, tokenizer_path, transformer.config.vocab_size);

    // build the Sampler
    Sampler sampler;
    build_sampler(&sampler, transformer.config.vocab_size);

    // encode the (string) prompt into tokens sequence, if any is given
    int *prompt_tokens = NULL; // the sequence of prompt tokens
    int num_prompt_tokens = 0; // the total number of prompt tokens
    if (prompt != NULL) {
        prompt_tokens = (int*)malloc((strlen(prompt)+1) * sizeof(int));
        encode(&tokenizer, prompt, prompt_tokens, &num_prompt_tokens);
    }

    // start the main loop
    long start = 0;  // used to time our code, only initialized after first iteration
    int next;        // will store the next token in the sequence
    int token = 1;   // init with token 1 (=BOS), as done in Llama-2 sentencepiece tokenizer
    int pos = 0;     // position in the sequence
    int bufferflush = 1; // token counter for flushing buffer
    static char outbuff[4096 * (6 + 2)] ; // buffersize is context length * average size of subwords + margin
    
    // Todo: we can do buffering without setvbuff, implement that
    // setvbuf is used to buffer output into outbuff instead of flushing to screen directly
    if (setvbuf(stdout, outbuff, _IOFBF, sizeof(outbuff)) != 0) {
    puts("Error: Buffer allocation!"); exit(EXIT_FAILURE);
    }

    double alpha = 1.0;

#if AD
    if(training_data){
        printf("Training. Yay!\n");

        // read the train.txt file
        FILE *train_file = fopen(training_data, "r");
        if (!train_file) {
            printf("Unable to open train.txt\n");
            return 1;
        }
        fseek(train_file, 0, SEEK_END);
        long length = ftell(train_file);
        // TODO: Manuel, check this
        transformer.dfile_size = length;
        fseek(train_file, 0, SEEK_SET);
        char* train_text = malloc(length);
        if (fread(train_text, 1, length, train_file) != length) {
            printf("Failed to read train.txt\n");
            return 1;
        }
        fclose(train_file);

        // float* dweightsacc_ptr = calloc(file_size - sizeof(Config)/sizeof(float));

        // greedily match with vocab
        for (int i = 0; i < length && pos < steps; ) {
            int maxlen = -1;
            int maxj = -1;

            for (int j = 0; j < transformer.config.vocab_size; j++) {
                int len = strlen(tokenizer.vocab[j]);
                // 
                if (strncmp(&train_text[i], tokenizer.vocab[j], len) == 0 && len > maxlen) {
                    maxlen = len;
                    maxj = j;
                }
            }
            // printf("Matched token %d = '%s' at position %d with length %d\n", maxj, vocab[maxj], i, maxlen);

            i += maxlen;


            int nexttok = maxj;
            // printf("nexttok: %d\n", nexttok);
            // printf("i: %d\n", i);

            // transformer(token, pos, &config, &state, &weights);

            float lres =  __enzyme_autodiff((void*)loss,
                            enzyme_primal_return,
                            enzyme_const, token,
                            enzyme_const, pos,
                            enzyme_const, &transformer.config,
                            enzyme_dup, &transformer.state, &transformer.dstate, 
                            enzyme_dup, &transformer.weights, &transformer.dweights,
                            nexttok,
                            enzyme_const, temperature);

            // printf("%s %d %f\n", nexttok == -1 ? "<INVALID>" : tokenizer.vocab[nexttok], pos, lres);
            // fflush(stdout);

            for (size_t i =0, end=(transformer.file_size - sizeof(Config))/sizeof(float); i<end; i++) {
                //if (fabs(transformer.dweights_ptr[i]) > 1000 || isnan(transformer.dweights_ptr[i])) {
                //    printf("%i %f\n", i, transformer.dweights_ptr[i]);
                //    exit(1);
                //}
                //if (fabs(transformer.dweights_ptr[i]) > 1e-2) {
                //    printf("%i %f %d\n", i, transformer.dweights_ptr[i], pos);
                //}
                transformer.weights_ptr[i] += alpha * transformer.dweights_ptr[i];
                transformer.dweights_ptr[i] = 0;
            }
            zero_run_state(&transformer.dstate, &transformer.config);

            token = maxj;
            pos++;
            // break;

        }

        printf("\n\nFinished fine-tuning.\n\n");

        pos = 0;
        zero_run_state(&transformer.state, &transformer.config);
        token = 1;
        printf("<s>\n"); // explicit print the initial BOS token (=1), stylistically symmetric
    }
#endif // AD

    // free_run_state(&state);
    // malloc_run_state(&state, &config);

    // while (pos < steps) {

    //     transformer(token, pos, &config, &state, &weights);
    //     // sample the next token
    //     if(temperature == 0.0f) {
    //         // greedy argmax sampling
    //         next = argmax(state.logits, config.vocab_size);
    //     } else {
    //         // apply the temperature to the logits
    //         for (int q=0; q<config.vocab_size; q++) { state.logits[q] /= temperature; }
    //         // apply softmax to the logits to get the probabilities for next token
    //         softmax(state.logits, config.vocab_size);
    //         // we now want to sample from this distribution to get the next token
    //         next = sample(state.logits, config.vocab_size);
    //     }
    //     // printf("%d\n", next);
    //     printf("%s", vocab[next]);
    //     fflush(stdout);

    //     token = next;
    //     pos++;

    // }

    while (pos < steps) {

        // forward the transformer to get logits for the next token
        float* logits = forward(token, pos, &transformer.config, &transformer.weights, &transformer.state);
        //float* logits = forward(&transformer, token, pos);
        //Config* p = &transformer->config;
        //TransformerWeights* w = &transformer->weights;
        //RunState* s = &transformer->state;

        // advance the state state machine
        if (pos < num_prompt_tokens) {
            // if we are still processing the input prompt, force the next prompt token
            next = prompt_tokens[pos];
        } else {
            // otherwise sample the next token from the logits
            next = sample(&sampler, logits, temperature, topp);
        }
        pos++;

        // data-dependent terminating condition: the BOS (1) token delimits sequences
        if (next == 1) { break; }

        // print the token as string, decode it with the Tokenizer object
        char* piece = decode(&tokenizer, token, next);
        printf("%s", piece);
        if (bufferflush==pos) { fflush(stdout); bufferflush+=buffertokens; } 
        token = next;

        // init the timer here because the first iteration can be slower
        if (start == 0) { start = time_in_ms(); }
    }
    printf("\n");
    fflush(stdout); // This could be in the if next break, and the print new line prepended to achieved tok/s
    // report achieved tok/s (pos-1 because the timer starts after first iteration)
    if (pos > 1) {
        long end = time_in_ms();
        if(stats){ fprintf(stderr, "achieved tok/s: %f\n", (pos-1) / (double)(end-start)*1000); } 
    }

    // memory and file handles cleanup
    if (prompt_tokens != NULL) { free(prompt_tokens); }
    free_sampler(&sampler);
    free_tokenizer(&tokenizer);
    free_transformer(&transformer);
    #if defined(COSMO_ZIP) || defined(INC_BIN) || defined(STRLIT)
    #ifdef LLOOP
    printf("\n");
    } // end of loop
    #endif
    #endif    
    return 0;
}
