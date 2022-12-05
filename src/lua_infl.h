/*
 * lua_infl.h
 *
 * @brief PICO-8 Lua code inflate routines (with minor modifications for ANSI C by bzt)
 * https://github.com/dansanderson/lexaloffle
 */

/* old compression format */
#define LITERALS 60
#define READ_VAL(val) {val = *in; in++;}
int decompress_mini(uint8_t *in_p, uint8_t *out_p, int max_len)
{
	char *literal = "^\n 0123456789abcdefghijklmnopqrstuvwxyz!#%(){}[]<>+=/*:;.,~_";
	int literal_index[256]; // map literals to 0..LITERALS-1. 0 is reserved (not listed in literals string)
	int block_offset;
	int block_length;
	int val;
	uint8_t *in = in_p;
	uint8_t *out = out_p;
	int len;

	// header tag ":c:"
	READ_VAL(val);
	READ_VAL(val);
	READ_VAL(val);
	READ_VAL(val);

	// uncompressed length
	READ_VAL(val);
	len = val * 256;
	READ_VAL(val);
	len += val;

	// compressed length (to do: use to check)
	READ_VAL(val);
	READ_VAL(val);

	memset(out_p, 0, max_len);

	if (len > max_len) return 1; // corrupt data

	while (out < out_p + len)
	{
		READ_VAL(val);

		if (val < LITERALS)
		{
			// literal
			if (val == 0)
			{
				READ_VAL(val);
				//printf("rare literal: %d\n", val);
				*out = val;
			}
			else
			{
				// printf("common literal: %d (%c)\n", literal[val], literal[val]);
				*out = literal[val];
			}
			out++;
		}
		else
		{
			// block
			block_offset = val - LITERALS;
			block_offset *= 16;
			READ_VAL(val);
			block_offset += val % 16;
			block_length = (val / 16) + 2;

			memcpy(out, out - block_offset, block_length);
			out += block_length;
		}
	}
	return out - out_p;
}
#undef LITERALS

/* new compression format */
#define PXA_MIN_BLOCK_LEN 3
#define BLOCK_LEN_CHAIN_BITS 3
#define BLOCK_DIST_BITS 5
#define TINY_LITERAL_BITS 4
#define PXA_READ_VAL(x)  getval(8)
static int bit = 1;
static int byte = 0;
static int dest_pos = 0;
static int src_pos = 0;
static uint8_t *dest_buf = NULL;
static uint8_t *src_buf = NULL;
static int getbit()
{
	int ret;

	ret = (src_buf[src_pos] & bit) ? 1 : 0;
	bit <<= 1;
	if (bit == 256)
	{
		bit = 1;
		src_pos ++;
	}
	return ret;
}
static int getval(int bits)
{
	int i;
	int val = 0;
	if (bits == 0) return 0;

	for (i = 0; i < bits; i++)
		if (getbit())
			val |= (1 << i);

	return val;
}
static int getchain(int link_bits, int max_bits)
{
	int i;
	int max_link_val = (1 << link_bits) - 1;
	int val = 0;
	int vv = max_link_val;
	int bits_read = 0;

	while (vv == max_link_val)
	{
		vv = getval(link_bits);
		bits_read += link_bits;
		val += vv;
		if (bits_read >= max_bits) return val; // next val is implicitly 0
	}

	return val;
}
static int getnum()
{
	int jump = BLOCK_DIST_BITS;
	int bits = jump;
	int src_pos_0 = src_pos;
	int bit_0 = bit;
	int val;

	// 1  15 bits // more frequent so put first
	// 01 10 bits
	// 00  5 bits
	bits = (3 - getchain(1, 2)) * BLOCK_DIST_BITS;

	val = getval(bits);

	if (val == 0 && bits == 10)
		return -1; // raw block marker

	return val;
}

int pxa_decompress(uint8_t *in_p, uint8_t *out_p, int max_len)
{
	uint8_t *dest;
	int i;
	int literal[256];
	int literal_pos[256];
	int dest_pos = 0;

	bit = 1;
	byte = 0;
	src_buf = in_p;
	src_pos = 0;

	for (i = 0; i < 256; i++)
		literal[i] = i;

	for (i = 0; i < 256; i++)
		literal_pos[literal[i]] = i;

	// header

	int header[8];
	for (i = 0; i < 8; i++)
		header[i] = PXA_READ_VAL();

	int raw_len  = header[4] * 256 + header[5];
	int comp_len = header[6] * 256 + header[7];

	// printf(" read raw_len:  %d\n", raw_len);
	// printf(" read comp_len: %d\n", comp_len);

	while (src_pos < comp_len && dest_pos < raw_len && dest_pos < max_len)
	{
		int block_type = getbit();

		// printf("%d %d\n", src_pos, block_type); fflush(stdout);

		if (block_type == 0)
		{
			// block

			int block_offset = getnum() + 1;

			if (block_offset == 0)
			{
				// 0.2.0j: raw block
				while (dest_pos < raw_len)
				{
					out_p[dest_pos] = getval(8);
					if (out_p[dest_pos] == 0) // found end -- don't advance dest_pos
						break;
					dest_pos ++;
				}
			}
			else
			{
				int block_len = getchain(BLOCK_LEN_CHAIN_BITS, 100000) + PXA_MIN_BLOCK_LEN;

				// copy // don't just memcpy because might be copying self for repeating pattern
				while (block_len > 0){
					out_p[dest_pos] = out_p[dest_pos - block_offset];
					dest_pos++;
					block_len--;
				}

				// safety: null terminator. to do: just do at end
				if (dest_pos < max_len-1)
					out_p[dest_pos] = 0;
			}
		}else
		{
			// literal

			int lpos = 0;
			int bits = 0;

			int safety = 0;
			while (getbit() == 1 && safety++ < 16)
			{
				lpos += (1 << (TINY_LITERAL_BITS + bits));
				bits ++;
			}

			bits += TINY_LITERAL_BITS;
			lpos += getval(bits);

			if (lpos > 255) return 0; // something wrong

			// grab character and write
			int c = literal[lpos];

			out_p[dest_pos] = c;
			dest_pos++;
			out_p[dest_pos] = 0;

			int i;
			for (i = lpos; i > 0; i--)
			{
				literal[i] = literal[i-1];
				literal_pos[literal[i]] ++;
			}
			literal[0] = c;
			literal_pos[c] = 0;
		}
	}


	return 0;
}

int is_compressed_format_header(uint8_t *dat)
{
	if (dat[0] == ':' && dat[1] == 'c' && dat[2] == ':' && dat[3] == 0) return 1;
	if (dat[0] == 0 && dat[1] == 'p' && dat[2] == 'x' && dat[3] == 'a') return 2;
	return 0;
}

// max_len should be 0x10000 (64k max code size)
// out_p should allocate 0x10001 (includes null terminator)
int pico8_code_section_decompress(uint8_t *in_p, uint8_t *out_p, int max_len)
{
	if (is_compressed_format_header(in_p) == 0) { memcpy(out_p, in_p, 0x3d00); out_p[0x3d00] = '\0'; return 0; } // legacy: no header -> is raw text
	if (is_compressed_format_header(in_p) == 1) return decompress_mini(in_p, out_p, max_len);
	if (is_compressed_format_header(in_p) == 2) return pxa_decompress (in_p, out_p, max_len);
	return 0;
}
