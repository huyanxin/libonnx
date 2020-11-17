#include <onnx.h>

enum auto_pad_t {
	AUTO_PAD_VALID		= 0,
	AUTO_PAD_NOTSET		= 1,
	AUTO_PAD_SAME_UPPER	= 2,
	AUTO_PAD_SAME_LOWER	= 3,
};

struct operator_pdata_t {
	enum auto_pad_t auto_pad;
	int ceil_mode;
	int storage_order;
	int * kernels;
	int nkernel;
	int * dilations;
	int ndilation;
	int * pads;
	int npad;
	int * strides;
	int nstride;

	int cpads[32];
};

static int MaxPool_init(struct onnx_node_t * n)
{
	struct operator_pdata_t * pdat;
	int64_t * ints;
	int i, l;

	if((n->ninput == 1) && (n->noutput >= 1))
	{
		pdat = malloc(sizeof(struct operator_pdata_t));
		if(pdat)
		{
			memset(pdat, 0, sizeof(struct operator_pdata_t));
			switch(shash(onnx_attribute_read_string(n, "auto_pad", "NOTSET")))
			{
			case 0x0e382d15: /* "VALID" */
				pdat->auto_pad = AUTO_PAD_VALID;
				break;
			case 0xc3966fc2: /* "NOTSET" */
				pdat->auto_pad = AUTO_PAD_NOTSET;
				break;
			case 0xcbbc7856: /* "SAME_UPPER" */
				pdat->auto_pad = AUTO_PAD_SAME_UPPER;
				break;
			case 0xcb192d33: /* "SAME_LOWER" */
				pdat->auto_pad = AUTO_PAD_SAME_LOWER;
				break;
			default:
				pdat->auto_pad = AUTO_PAD_NOTSET;
				break;
			}
			pdat->ceil_mode = onnx_attribute_read_int(n, "ceil_mode", 0);
			pdat->storage_order = onnx_attribute_read_int(n, "storage_order", 0);
			pdat->nkernel = onnx_attribute_read_ints(n, "kernel_shape", &ints);
			if(pdat->nkernel > 0)
			{
				pdat->kernels = malloc(sizeof(int) * pdat->nkernel);
				for(i = 0; i < pdat->nkernel; i++)
					pdat->kernels[i] = ints[i];
			}
			pdat->ndilation = pdat->nkernel;
			pdat->dilations = malloc(sizeof(int) * pdat->ndilation);
			if(pdat->dilations)
			{
				l = onnx_attribute_read_ints(n, "dilations", &ints);
				for(i = 0; i < l; i++)
					pdat->dilations[i] = ints[i];
				for(; i < pdat->npad; i++)
					pdat->dilations[i] = 1;
			}
			pdat->npad = pdat->nkernel * 2;
			pdat->pads = malloc(sizeof(int) * pdat->npad);
			if(pdat->pads)
			{
				l = onnx_attribute_read_ints(n, "pads", &ints);
				for(i = 0; i < l; i++)
					pdat->pads[i] = ints[i];
				for(; i < pdat->npad; i++)
					pdat->pads[i] = 0;
			}
			pdat->nstride = pdat->nkernel;
			pdat->strides = malloc(sizeof(int) * pdat->nstride);
			if(pdat->strides)
			{
				l = onnx_attribute_read_ints(n, "strides", &ints);
				for(i = 0; i < l; i++)
					pdat->strides[i] = ints[i];
				for(; i < pdat->nstride; i++)
					pdat->strides[i] = 1;
			}
			n->priv = pdat;
			return 1;
		}
	}
	return 0;
}

static int MaxPool_exit(struct onnx_node_t * n)
{
	struct operator_pdata_t * pdat = (struct operator_pdata_t *)n->priv;

	if(pdat)
	{
		if(pdat->kernels)
			free(pdat->kernels);
		if(pdat->dilations)
			free(pdat->dilations);
		if(pdat->pads)
			free(pdat->pads);
		if(pdat->strides)
			free(pdat->strides);
		free(pdat);
	}
	return 1;
}

static int MaxPool_reshape(struct onnx_node_t * n)
{
	struct operator_pdata_t * pdat = (struct operator_pdata_t *)n->priv;
	struct onnx_tensor_t * x = n->inputs[0];
	struct onnx_tensor_t * y = n->outputs[0];
	int ndim = x->ndim;
	int dims[ndim];
	int begin, end;
	int stride, kernel, needed;
	int i;

	memcpy(pdat->cpads, pdat->pads, sizeof(int) * pdat->npad);
    for(i = 0; i < ndim; i++)
    {
    	if(i < 2)
    		dims[i] = x->dims[i];
    	else
    	{
			begin = i - 2;
			end = begin + pdat->nkernel;
			stride = pdat->strides[begin];
			kernel = pdat->kernels[begin];
			switch(pdat->auto_pad)
			{
			case AUTO_PAD_VALID:
				pdat->cpads[begin] = 0;
				pdat->cpads[end] = 0;
				dims[i] = ceilf((x->dims[i] - kernel + 1) / stride);
				break;
			case AUTO_PAD_NOTSET:
				if(pdat->ceil_mode)
					dims[i] = ceilf((x->dims[i] + pdat->cpads[begin] + pdat->cpads[end] - kernel) / stride + 1);
				else
					dims[i] = floorf((x->dims[i] + pdat->cpads[begin] + pdat->cpads[end] - kernel) / stride + 1);
				break;
			case AUTO_PAD_SAME_UPPER:
				needed = ((x->dims[i] + stride - 1) / stride - 1) * stride + kernel - x->dims[i];
				pdat->cpads[begin] = floorf(needed / 2);
				pdat->cpads[end] = needed - pdat->cpads[begin];
				dims[i] = ceilf(x->dims[i] / stride);
				break;
			case AUTO_PAD_SAME_LOWER:
				needed = ((x->dims[i] + stride - 1) / stride - 1) * stride + kernel - x->dims[i];
				pdat->cpads[begin] = floorf((needed + 1) / 2);
				pdat->cpads[end] = needed - pdat->cpads[begin];
				dims[i] = ceilf(x->dims[i] / stride);
				break;
			default:
				break;
			}
    	}
    }
    return onnx_tensor_reshape(y, dims, ndim, x->type);
}

static inline int dim_next(int ndim, int * dims, int * dim_max)
{
	if(ndim == 0)
		return 0;
	while(1)
	{
		ndim = ndim - 1;
		dims[ndim] += 1;
		if(dims[ndim] < dim_max[ndim])
			return 1;
		else
		{
			if(ndim == 0)
				return 0;
			dims[ndim] = 0;
		}
	}
}

static inline int dim_offset(int ndim, int * dims, int * dim_max)
{
	int o, s;
	int i;

	for(i = ndim - 1, o = 0, s = 1; i >= 0; i--)
	{
		o += dims[i] * s;
		s *= dim_max[i];
	}
	return o;
}

static void MaxPool_float16(struct onnx_node_t * n)
{
	struct operator_pdata_t * pdat = (struct operator_pdata_t *)n->priv;
	struct onnx_tensor_t * x = n->inputs[0];
	struct onnx_tensor_t * y = n->outputs[0];
	uint16_t * px = (uint16_t *)x->datas;
	uint16_t * py = (uint16_t *)y->datas;
	float maxv, v;
	int k_dim[x->ndim - 2];
	int i_dim[x->ndim];
	int o_dim[x->ndim];
	int b_dim[x->ndim];
	int i;

	memset(o_dim, 0, sizeof(o_dim));
	do {
		for(i = 2; i < x->ndim; ++i)
			b_dim[i] = o_dim[i] * pdat->strides[i - 2] - pdat->cpads[i - 2];
		maxv = -FLT_MAX;
		memset(k_dim, 0, sizeof(k_dim));
		do {
			i_dim[0] = o_dim[0];
			i_dim[1] = o_dim[1];
			for(i = 2; i < x->ndim; ++i)
				i_dim[i] = b_dim[i] + k_dim[i - 2];
			for(i = 0; i < x->ndim; ++i)
			{
				if((i_dim[i] < 0) || (i_dim[i] >= x->dims[i]))
					break;
			}
			if(i >= x->ndim)
			{
				v = float16_to_float32(px[dim_offset(x->ndim, i_dim, x->dims)]);
				maxv = fmaxf(v, maxv);
			}
		} while(dim_next(x->ndim - 2, k_dim, pdat->kernels));
		py[dim_offset(x->ndim, o_dim, y->dims)] = float32_to_float16(maxv);
	} while(dim_next(x->ndim, o_dim, y->dims));
}

static void MaxPool_float32(struct onnx_node_t * n)
{
	struct operator_pdata_t * pdat = (struct operator_pdata_t *)n->priv;
	struct onnx_tensor_t * x = n->inputs[0];
	struct onnx_tensor_t * y = n->outputs[0];
	float * px = (float *)x->datas;
	float * py = (float *)y->datas;
	float maxv, v;
	int k_dim[x->ndim - 2];
	int i_dim[x->ndim];
	int o_dim[x->ndim];
	int b_dim[x->ndim];
	int i;

	memset(o_dim, 0, sizeof(o_dim));
	do {
		for(i = 2; i < x->ndim; ++i)
			b_dim[i] = o_dim[i] * pdat->strides[i - 2] - pdat->cpads[i - 2];
		maxv = -FLT_MAX;
		memset(k_dim, 0, sizeof(k_dim));
		do {
			i_dim[0] = o_dim[0];
			i_dim[1] = o_dim[1];
			for(i = 2; i < x->ndim; ++i)
				i_dim[i] = b_dim[i] + k_dim[i - 2];
			for(i = 0; i < x->ndim; ++i)
			{
				if((i_dim[i] < 0) || (i_dim[i] >= x->dims[i]))
					break;
			}
			if(i >= x->ndim)
			{
				v = px[dim_offset(x->ndim, i_dim, x->dims)];
				maxv = fmaxf(v, maxv);
			}
		} while(dim_next(x->ndim - 2, k_dim, pdat->kernels));
		py[dim_offset(x->ndim, o_dim, y->dims)] = maxv;
	} while(dim_next(x->ndim, o_dim, y->dims));
}

static void MaxPool_float64(struct onnx_node_t * n)
{
	struct operator_pdata_t * pdat = (struct operator_pdata_t *)n->priv;
	struct onnx_tensor_t * x = n->inputs[0];
	struct onnx_tensor_t * y = n->outputs[0];
	double * px = (double *)x->datas;
	double * py = (double *)y->datas;
	double maxv, v;
	int k_dim[x->ndim - 2];
	int i_dim[x->ndim];
	int o_dim[x->ndim];
	int b_dim[x->ndim];
	int i;

	memset(o_dim, 0, sizeof(o_dim));
	do {
		for(i = 2; i < x->ndim; ++i)
			b_dim[i] = o_dim[i] * pdat->strides[i - 2] - pdat->cpads[i - 2];
		maxv = -DBL_MAX;
		memset(k_dim, 0, sizeof(k_dim));
		do {
			i_dim[0] = o_dim[0];
			i_dim[1] = o_dim[1];
			for(i = 2; i < x->ndim; ++i)
				i_dim[i] = b_dim[i] + k_dim[i - 2];
			for(i = 0; i < x->ndim; ++i)
			{
				if((i_dim[i] < 0) || (i_dim[i] >= x->dims[i]))
					break;
			}
			if(i >= x->ndim)
			{
				v = px[dim_offset(x->ndim, i_dim, x->dims)];
				maxv = fmax(v, maxv);
			}
		} while(dim_next(x->ndim - 2, k_dim, pdat->kernels));
		py[dim_offset(x->ndim, o_dim, y->dims)] = maxv;
	} while(dim_next(x->ndim, o_dim, y->dims));
}

void resolver_default_op_MaxPool(struct onnx_node_t * n)
{
	switch(n->inputs[0]->type)
	{
	case ONNX_TENSOR_TYPE_FLOAT16:
		n->init = MaxPool_init;
		n->exit = MaxPool_exit;
		n->reshape = MaxPool_reshape;
		n->operator = MaxPool_float16;
		break;
	case ONNX_TENSOR_TYPE_FLOAT32:
		n->init = MaxPool_init;
		n->exit = MaxPool_exit;
		n->reshape = MaxPool_reshape;
		n->operator = MaxPool_float32;
		break;
	case ONNX_TENSOR_TYPE_FLOAT64:
		n->init = MaxPool_init;
		n->exit = MaxPool_exit;
		n->reshape = MaxPool_reshape;
		n->operator = MaxPool_float64;
		break;
	default:
		break;
	}
}
