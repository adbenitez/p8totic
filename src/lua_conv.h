/*
 * lua_conv.h
 *
 * Copyright (C) 2022 bzt (bztsrc@gitlab) MIT license
 * Copyright (C) 2022 musurca (PICO-8 wrapper library)
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * @brief PICO-8 Lua to TIC-80 converter
 */

#define TOK_IMPLEMENTATION
#include "tok.h"

/* PICO-8 codepage to UTF-8 UNICODE
 * Array maps PICO-8 characters 16-255 to UTF-8 strings (index 0 = char 16, index 239 = char 255)
 * 
 * ASCII Fallback Replacements (for TIC-80 compatibility):
 * - Char 130 (0x82): 🐱 → "^.^"  (cat face)
 * - Char 137 (0x89): 웃  → ":)"   (smiling face)
 * - Char 140 (0x8C): 😐 → ":I"   (neutral face)
 * - Char 142 (0x8E): 🅾️ → "(O)"  (O button)
 * - Char 151 (0x97): ❎ → "(X)"  (X button)
 * - Arrows: Emoji variants replaced with Unicode arrows (↑↓←→) without variation selectors
 * 
 * Other characters (block graphics, Japanese, symbols) kept as UTF-8 since TIC-80 renders them.
 */
const char *pico_utf8[] = {
    /* 16-31: PICO-8 special characters */
    "▮","■","□","⁙","⁘","‖","◀","▶","「","」","¥","•","、","。","゛","゜",
    /* 32-127: Standard ASCII */
    " ","!","\"","#","$","%","&","'","(",")","*","+",",","-",".","/",
    "0","1","2","3","4","5","6","7","8","9",":",";","<","=",">","?",
    "@","A","B","C","D","E","F","G","H","I","J","K","L","M","N","O",
    "P","Q","R","S","T","U","V","W","X","Y","Z","[","\\","]","^","_",
    "`","a","b","c","d","e","f","g","h","i","j","k","l","m","n","o",
    "p","q","r","s","t","u","v","w","x","y","z","{","|","}","~","○",
    /* 128-159: Extended characters with ASCII fallbacks for emojis */
    "█","▒","^.^","↓","░","✽","●","♥","☉",":)","⌂","←",":I","♪","(O)","◆",
    "…","→","★","⧗","↑","ˇ","∧","(X)","▤","▥","あ","い","う","え","お","か",
    /* 160-255: Japanese Hiragana and Katakana */
    "き","く","け","こ","さ","し","す","せ","そ","た","ち","つ","て","と","な","に",
    "ぬ","ね","の","は","ひ","ふ","へ","ほ","ま","み","む","め","も","や","ゆ","よ",
    "ら","り","る","れ","ろ","わ","を","ん","っ","ゃ","ゅ","ょ","ア","イ","ウ","エ",
    "オ","カ","キ","ク","ケ","コ","サ","シ","ス","セ","ソ","タ","チ","ツ","テ","ト",
    "ナ","ニ","ヌ","ネ","ノ","ハ","ヒ","フ","ヘ","ホ","マ","ミ","ム","メ","モ","ヤ",
    "ユ","ヨ","ラ","リ","ル","レ","ロ","ワ","ヲ","ン","ッ","ャ","ュ","ョ","◜","◝"
};

/**
 * Replace PICO-8 characters with UTF-8
 */
int pico_lua_to_utf8(uint8_t *dst, int maxlen, uint8_t *src, int srclen)
{
    uint8_t *orig = dst, *end = dst + maxlen - 6;
    int i, j;
    unsigned int k;

    for(i = 0; src[i] && i < srclen && dst < end; i++) {
        k = (unsigned int)((uint8_t)src[i]);
        if(k < 16) {
            /* control codes are the same */
            *dst++ = src[i];
        } else {
            j = strlen(pico_utf8[k - 16]);
            memcpy(dst, pico_utf8[k - 16], j);
            dst += j;
        }
    }
    *dst = 0;
    return (int)((uintptr_t)dst - (uintptr_t)orig);
}

/* configure lua token types here */
char *lua_com[] = { "\\-\\-.*?$", NULL };
char *lua_ops[] = { "::=", "\\.\\.\\.", "\\.\\.", "\\.\\.=", "[~=\\<\\>\\+\\-\\*\\/%&\\^\\|\\\\!][:=]?", NULL };
char *lua_num[] = { "[\\-]?[1-9][0-9]*", "[\\-]?[0-9][0-9bx]?[0-9\\.a-f]*", NULL };
char *lua_str[] = { "\"", "\'", NULL };
char *lua_sep[] = { "[", "]", "{", "}", ",", ";", ":", NULL };
char *lua_typ[] = { "false", "local", "nil", "true", NULL };
char *lua_kws[] = { "and", "break", "do", "else", "elseif", "end", "for", "function", "if", "in", "not", "or",
    "repeat", "return", "then", "until", "while", NULL };
char **lua_rules[] = { lua_com, NULL, lua_ops, lua_num, lua_str, lua_sep, lua_typ, lua_kws };

/**
 * Lua syntax converter
 *   src is zero terminated (but you can also use srclen)
 *   dst is at least 512k (but use maxlen)
 */
static int pico_lua_to_tic_lua(char *dst, int maxlen, char *src, int srclen)
{
    tok_t tok;
    int i, j, k, l, m, len;
    char tmp[256], *c;

    /* tokenize Lua string */
    if(!tok_new(&tok, lua_rules, src, srclen)) {
        fprintf(stderr, "p8totic: unable to tokenize??? Should never happen!\r\n");
        memcpy(dst, src, srclen);
        dst[srclen] = 0;
        return srclen;
    }

    /* FIXME: if there's any more syntax or API difference between PICO-8 and TIC-80, replace tokens here.
     * Also, if you add a Lua API syntax change, remove the relevant part from the helper lib below! */
    for(i = 0; i < tok.num; i++) {
        /*** syntax changes ***/
        /* replace "!=" with "~=" */
        if(tok.tokens[i] && tok.tokens[i][0] == TOK_OPERATOR && tok.tokens[i][1] == '!' && tok.tokens[i][2] == '=' &&
          !tok.tokens[i][3]) tok.tokens[i][1] = '~';
        /* convert shorthand operators, like "var +=" -> "var = var +" */
        if(tok.tokens[i][0] == TOK_OPERATOR && strchr("+-*/%&^\\.", tok.tokens[i][1]) && strchr(tok.tokens[i] + 1, '=')) {
            c = strchr(tok.tokens[i] + 1, '='); *c = 0;
            /* variable might consist of multiple tokens, eg. "var[i].field +=" so we need to copy all tokens between */
            for(j = i - 1, m = 0; j > 0 && (m || tok.tokens[j][0] != TOK_VARIABLE); j--) {
                if(tok.tokens[j][1] == ']' || tok.tokens[j][1] == ')') m++;
                if(tok.tokens[j][1] == '[' || tok.tokens[j][1] == '(') m--;
                tok_insert(&tok, i, tok.tokens[j][0], tok.tokens[j] + 1);
            }
            tok_insert(&tok, i, tok.tokens[j][0], tok.tokens[j] + 1);
            tok_insert(&tok, i, TOK_OPERATOR, "=");
        }
        /* replace "\" with "//" */
        if(tok.tokens[i][0] == TOK_OPERATOR && !strcmp(tok.tokens[i] + 1, "\\")) {
            tok_replace(&tok, i, TOK_OPERATOR, "//");
        }
        /* replace "if(expr) cmd" with "if(expr) then cmd end" */
        if(tok_match(&tok, i, 2, TOK_KEYWORD, TOK_SEPARATOR) && !strcmp(tok.tokens[i] + 1, "if") &&
          ((i + 1 < tok.num && tok.tokens[i + 1][1] == '(') || (i + 2 < tok.num && tok.tokens[i + 2][1] == '('))) {
            j = i + (tok.tokens[i + 1][1] == '(' ? 2 : 3);
            k = tok_next(&tok, j, TOK_SEPARATOR, ")");
            if(k < 0) k = tok_next(&tok, j, TOK_SEPARATOR, ") ");
            if(k > i && k + 1 < tok.num) {
                for(l = k + 1; l < tok.num && (tok.tokens[l][0] != TOK_KEYWORD || strcmp(tok.tokens[l] + 1, "then")); l++)
                    if(strchr(tok.tokens[l] + 1, '\n')) { l = 0; break; }
                /* if there was no "then" before the newline */
                if(!l) {
                    /* add "then" */
                    tok_insert(&tok, k + 1, TOK_KEYWORD, "then ");
                    /* find next token with a newline character */
                    for(j = k + 2; j < tok.num && !strchr(tok.tokens[j] + 1, '\n'); j++);
                    if(j < tok.num) {
                        /* find newline character and insert "end" before */
                        for(k = 1, l = m = 0; l < 255 && tok.tokens[j][k]; k++, l++) {
                            if(!m && tok.tokens[j][k] == '\n') { memcpy(tmp + l, " end", 4); l += 4; m = 1; }
                            tmp[l] = tok.tokens[j][k];
                        }
                        tmp[l] = 0;
                        tok_replace(&tok, j, tok.tokens[j][0], tmp);
                    }
                }
            }
        }
        /* add an extra space between numbers and keywords */
        if(tok_match(&tok, i, 2, TOK_NUMBER, TOK_KEYWORD)) {
            tok_insert(&tok, i + 1, TOK_SEPARATOR, " ");
        }
        /*** API function name changes ***/
        if(tok.tokens[i] && tok.tokens[i][0] == TOK_FUNCTION) {
            /* replace dget and dset with pmem */
            if(!strcmp(tok.tokens[i] + 1, "dget") || !strcmp(tok.tokens[i] + 1, "dset"))
                strcpy(tok.tokens[i] + 1, "pmem");
            /* remove cartdata() */
            if(!strcmp(tok.tokens[i] + 1, "cartdata")) {
                j = tok_next(&tok, i + 2, TOK_SEPARATOR, ")");
                if(j > i) {
                    for(; j >= i; j--)
                        tok_delete(&tok, i);
                }
            }
            /* replace shr() and shl() functions with infix operators, like "shl(a,b)" -> "(a<<b)" */
            if(!strcmp(tok.tokens[i] + 1, "shl") || !strcmp(tok.tokens[i] + 1, "shr")) {
                j = tok_next(&tok, i + 2, TOK_SEPARATOR, ",");
                if(j > i) {
                    tok_replace(&tok, j, TOK_OPERATOR, !strcmp(tok.tokens[i] + 1, "shl") ? "<<" : ">>");
                    tok_delete(&tok, i);
                }
            }
            /* replace music(track,...) -> music(track) (the other arguments not supported on TIC-80) */
            if(!strcmp(tok.tokens[i] + 1, "music")) {
                j = tok_next(&tok, i + 2, TOK_SEPARATOR, ",");
                if(j > i) {
                    k = tok_next(&tok, j, TOK_SEPARATOR, ")");
                    if(k > j) {
                        for(k -= j; k; k--)
                            tok_delete(&tok, j);
                    }
                }
            }
            /* replace mapdraw() -> map() */
            if(!strcmp(tok.tokens[i] + 1, "mapdraw")) tok_replace(&tok, i, TOK_FUNCTION, "map");
            /* replace misc functions */
            if(!strcmp(tok.tokens[i] + 1, "tostr")) tok_replace(&tok, i, TOK_FUNCTION, "tostring");
            /* replace math functions */
            if(!strcmp(tok.tokens[i] + 1, "srand")) tok_replace(&tok, i, TOK_FUNCTION, "math.randomseed");
            if(!strcmp(tok.tokens[i] + 1, "sqrt"))  tok_replace(&tok, i, TOK_FUNCTION, "math.sqrt");
            if(!strcmp(tok.tokens[i] + 1, "abs"))   tok_replace(&tok, i, TOK_FUNCTION, "math.abs");
            if(!strcmp(tok.tokens[i] + 1, "min"))   tok_replace(&tok, i, TOK_FUNCTION, "math.min");
            if(!strcmp(tok.tokens[i] + 1, "max"))   tok_replace(&tok, i, TOK_FUNCTION, "math.max");
            if(!strcmp(tok.tokens[i] + 1, "flr"))   tok_replace(&tok, i, TOK_FUNCTION, "math.floor");
            if(!strcmp(tok.tokens[i] + 1, "rnd"))   tok_replace(&tok, i, TOK_FUNCTION,
                i + 3 < tok.num && tok.tokens[i + 2][1] == ')' && tok.tokens[i + 3][1] == '*' ? "math.random" : "math.random()*");
        }
        if(tok.tokens[i] && tok.tokens[i][0] == TOK_VARIABLE) {
            if(!strcmp(tok.tokens[i] + 1, "pi"))    tok_replace(&tok, i, TOK_VARIABLE, "math.pi");
        }
    }

    /* detokenize, aka. serialize into a string */
    if((len = tok_tostr(&tok, dst, maxlen)) < 1) {
        fprintf(stderr, "p8totic: unable to serialize??? Should never happen!\r\n");
        len = 0;
    }
    dst[len] = 0;
    tok_free(&tok);
    return len;
}

/**
 * PICO-8 Wrapper for the TIC-80 Computer
 * by @musurca
 * https://github.com/musurca/pico2tic
 *
 * by bzt: reformated as a C string, and parts removed that are already converted
 */
char p8totic_lua[] =
"-- Converted from PICO-8 cartridge by --\n"
"--  https://bztsrc.gitlab.io/p8totic  --\n"
"\n"
/*
--PICO-8 Wrapper for the TIC-80 Computer
--by @musurca
----------------------------------------
-- Wraps the PICO-8 API for ease of porting games
-- to the TIC-80. Favors compatibility over performance.
----------------------------------------
--known issues:
-- * swapping elements in the screen palette--e.g. pal(a,b,1)--doesn't work properly yet. However, pal(a,b) does work
-- * flip_x and flip_y are currently ignored sspr() only spr() supports them
-- * music() and flip() do nothing. sfx() does not take into account offset
-- * stat(1) always returns "0.5"
*/
/*
"--set palette\n"
"PAL_PICO8=\"0000001D2B537E2553008751AB52365F574FC2C3C7FFF1E8FF004DFFA300FFEC2700E43629ADFF83769CFF77A8FFCCAA\"\n"
"function PICO8_PALETTE()\n"
"	for i=0,15 do\n"
"		local r=tonumber(string.sub(PAL_PICO8,i*6+1,i*6+2),16)\n"
"		local g=tonumber(string.sub(PAL_PICO8,i*6+3,i*6+4),16)\n"
"		local b=tonumber(string.sub(PAL_PICO8,i*6+5,i*6+6),16)\n"
"		poke(0x3FC0+(i*3)+0,r)\n"
"		poke(0x3FC0+(i*3)+1,g)\n"
"		poke(0x3FC0+(i*3)+2,b)\n"
"	end	\n"
"end\n"
"\n"
"--sound\n"
*/
"__sfx=sfx\n"
"function sfx(n,channel,offset)\n"
/*" --does not support offset as of 0.18.0\n"*/
"	if n==-2 then\n"
"	 __sfx(-1)\n"
"	elseif n==-1 then\n"
"	 __sfx(-1,nil,nil,channel)\n"
"	else\n"
"	 __sfx(n,28,-1,channel)\n"
"	end\n"
"end\n"
"\n"
/*
"function music(n,fadems,channelmask)\n"
" --do nothing as of 0.18.0\n"
"end\n"
"\n"
*/
/*"--utility\n"*/
"function stat(i)\n"
" if i==0 then\n"
"	 return collectgarbage(\"count\")\n"
"	end\n"
" return 0.5\n"
"end\n"
"\n"
/*"--strings\n"*/
"function sub(str,i,j)\n"
" return str:sub(i,j)\n"
"end\n"
"\n"
/*"--permanent cart mem\n"*/
/*
"function cartdata(id)\n"
" --do nothing\n"
"end\n"
"\n"
"function dget(i)\n"
" return pmem(i)\n"
"end\n"
"\n"
"function dset(i,val)\n"
" pmem(i,val)\n"
"end\n"
"\n"
"--tables\n"
*/
"add=table.insert\n"
"\n"
"function all(list)\n"
"  local i = 0\n"
"  return function() i = i + 1; return list[i] end\n"
"end\n"
"\n"
/*"count=table.getn\n"*/
"function count(t, value)\n"
"	if value == nil then\n"
"		return #t\n"
"	else\n"
"		local c = 0\n"
"		for i = 1, #t do\n"
"			if t[i] == value then c = c + 1 end\n"
"		end\n"
"		return c\n"
"   end\n"
"end\n"
"\n"
"function del(t,a)\n"
"	for i,v in ipairs(t) do\n"
"		if v==a then\n"
"			t[i]=t[#t]\n"
"			t[#t]=nil\n"
"			return\n"
"		end\n"
"	end\n"
"end\n"
"\n"
"function foreach(t, f)\n"
"	for v in all(t) do\n"
"		f(v)\n"
"	end\n"
"end\n"
"\n"
"if mt ~= nil then\n"
"	mt = {}\n"
"end\n"
"\n"
/*"--math\n"
"srand=math.randomseed\n"
"sqrt=math.sqrt\n"
"abs=math.abs\n"
"min=math.min\n"
"max=math.max\n"
"flr=math.floor\n"
"pi=math.pi\n"
"\n"
"function rnd(a)\n"
" a=a or 1\n"
" return math.random()*a\n"
"end\n"
"\n"
*/
"function sgn(a)\n"
" if a>=0 then return 1 end\n"
"	return -1\n"
"end\n"
"\n"
"function cos(a)\n"
" return math.cos(2*math.pi*a)\n"
"end\n"
"\n"
"function sin(a)\n"
" return -math.sin(2*math.pi*a)\n"
"end\n"
"\n"
"function atan2(a,b)\n"
" b=b or 1\n"
" return math.atan(a,b)/(2*math.pi)\n"
"end\n"
"\n"
"function mid(a,b,c)\n"
" if a<=b and a<=c then return math.max(a,math.min(b,c))\n"
"	elseif b<=a and b<=c then return math.max(b,math.min(a,c)) end\n"
"	return math.max(c,math.min(a,b))\n"
"end\n"
"\n"
"function band(a,b)\n"
" return math.floor(a)&math.floor(b)\n"
"end\n"
"\n"
"function bor(a,b)\n"
" return math.floor(a)|math.floor(b)\n"
"end\n"
"\n"
"function bxor(a,b)\n"
" return math.floor(a)^math.floor(b)\n"
"end\n"
"\n"
"function bnot(a,b)\n"
" return math.floor(a)~math.floor(b)\n"
"end\n"
"\n"
/*
"function shl(a,b)\n"
" return a<<b\n"
"end\n"
"\n"
"function shr(a,b)\n"
" return a>>b\n"
"end\n"
"\n"
"--graphics\n"
*/
"__p8_color=7\n"
"__p8_ctrans={true,false,false,false,false,false,false,false,\n"
"             false,false,false,false,false,false,false,false}\n"
"__p8_camera_x=0\n"
"__p8_camera_y=0\n"
"__p8_cursor_x=0\n"
"__p8_cursor_y=0\n"
"__p8_sflags={}\n"
"for i=1,256 do\n"
" __p8_sflags[i]=0\n"
"end\n"
"\n"
"function camera(cx,cy)\n"
" cx=cx or 0\n"
"	cy=cy or 0\n"
"	__p8_camera_x=-math.floor(cx)\n"
"	__p8_camera_y=-math.floor(cy)\n"
"end\n"
"\n"
"function cursor(cx,cy)\n"
" cx=cx or 0\n"
"	cy=cy or 0\n"
"	__p8_cursor_x=math.floor(cx)\n"
"	__p8_cursor_y=math.floor(cy)\n"
"end\n"
"\n"
"function __p8_coord(x,y)\n"
" return math.floor(x+__p8_camera_x),\n"
"	       math.floor(y+__p8_camera_y)\n"
"end\n"
"\n"
"__print=print\n"
"function print(str,x,y,c)\n"
" x=x or __p8_cursor_x\n"
"	y=y or __p8_cursor_y\n"
"	c=c or __p8_color\n"
"	c=peek4(0x7FE0+c)\n"
"	__print(str,x,y,c)\n"
"	__p8_cursor_y=y+8\n"
"end\n"
"\n"
"function color(c)\n"
" c=c or 7\n"
"	__p8_color=math.floor(c%16)\n"
"end\n"
"\n"
"function pal(c0,c1,type)\n"
" c0=c0 or -1\n"
"	c1=c1 or -1\n"
"	type=type or 0\n"
"	\n"
"	if c0<0 and c1<0 then\n"
"	 if type==0 then\n"
"		 for i=0,15 do\n"
"		  poke4(0x7FE0+i,i)\n"
"		 end\n"
"	 end\n"
"	else\n"
"	 c0=math.floor(c0%16)\n"
"	 if c1<0 then\n"
"		 c1=c0\n"
"		end\n"
"		c1=math.floor(c1%16)\n"
"		if type==0 then\n"
"		 poke4(0x7FE0+c0,c1)\n"
"	 else\n"
"		 local stri\n"
"			for i=0,5 do\n"
"			 stri=#__p8_pal-(c1+1)*6+i\n"
"			 poke4(0x3FC0*2+#__p8_pal-(c0+1)*6+i,tonumber(__p8_pal:sub(stri,stri),16))\n"
"			end\n"
"		end\n"
"	end\n"
"end\n"
"\n"
"function palt(c,trans)\n"
" c=c or -1\n"
"	if c<0 then -- reset\n"
"	 __p8_ctrans[1]=true\n"
"		for i=2,16 do\n"
"		 __p8_ctrans[i]=false\n"
"		end\n"
"	else\n"
"	 __p8_ctrans[math.floor(c%16)+1]=trans\n"
"	end\n"
"end\n"
"\n"
"function pset(x,y,c)\n"
" c=c or __p8_color\n"
"	c=peek4(0x7FE0+c)\n"
"	x,y=__p8_coord(x,y)\n"
" poke4(y*240+x,c) 	\n"
"end\n"
"\n"
"function pget(x,y)\n"
" x,y=__p8_coord(x,y)\n"
"	return peek4(y*240+x)\n"
"end\n"
"\n"
"__rect=rect\n"
"function rectfill(x0,y0,x1,y1,c)\n"
"	c=c or __p8_color\n"
"	c=peek4(0x7FE0+c)\n"
"	x0,y0=__p8_coord(x0,y0)\n"
"	x1,y1=__p8_coord(x1,y1)\n"
"	local w,h=x1-x0,y1-y0\n"
"	__rect(x0,y0,w+sgn(w),h+sgn(h),c)\n"
"end\n"
"\n"
"function rect(x0,y0,x1,y1,c)\n"
" c=c or __p8_color\n"
" c=peek4(0x7FE0+c)\n"
"	x0,y0=__p8_coord(x0,y0)\n"
"	x1,y1=__p8_coord(x1,y1)\n"
"	local w,h=x1-x0,y1-y0\n"
"	rectb(x0,y0,w+sgn(w),h+sgn(h),c) \n"
"end\n"
"\n"
"__circ=circ\n"
"function circfill(x,y,r,c)\n"
" c=c or __p8_color\n"
"	c=peek4(0x7FE0+c)\n"
"	x,y=__p8_coord(x,y)\n"
"	__circ(x,y,r,c)\n"
"end\n"
"\n"
"function circ(x,y,r,c)\n"
" c=c or __p8_color\n"
"	c=peek4(0x7FE0+c)\n"
"	x,y=__p8_coord(x,y)\n"
"	circb(x,y,r,c)\n"
"end\n"
"\n"
"__line=line\n"
"function line(x0,y0,x1,y1,c)\n"
" c=c or __p8_color\n"
" c=peek4(0x7FE0+c)\n"
"	x0,y0=__p8_coord(x0,y0)\n"
"	x1,y1=__p8_coord(x1,y1)\n"
" __line(x0,y0,x1,y1,c)\n"
"end\n"
"\n"
"function ovalfill(x0, y0, x1, y1, color)\n"
"	local cx = math.floor((x0 + x1) / 2)\n"
"	local cy = math.floor((y0 + y1) / 2)\n"
"	local rx = math.floor(math.abs(x1 - x0) / 2)\n"
"	local ry = math.floor(math.abs(y1 - y0) / 2)\n"
"	elli(cx, cy, rx, ry, color)\n"
"end\n"
"\n"
"function sspr(sx,sy,sw,sh,dx,dy,dw,dh) -- todo\n"
" dw=dw or sw\n"
"	dh=dh or sh\n"
" dx,dy=__p8_coord(dx,dy)\n"
"	if dx>240 or dy>136 then return end\n"
"	local xscale,yscale=dw/sw,dh/sh	\n"
"	local startx,starty,c=0,0\n"
" if dx<0 then startx=-dx end\n"
"	if dy<0 then starty=-dy end\n"
"	if dx+dw>240 then dw=240-dx end\n"
"	if dy+dh>136 then dh=136-dy end\n"
"	for x=startx,dw-1 do\n"
"	 for y=starty,dh-1 do\n"
"		 c=sget(sx+x/xscale,sy+y/yscale)\n"
"			c=peek4(0x7FE0+c)\n"
"			if not __p8_ctrans[c+1] then\n"
"		  poke4((dy+y)*240+dx+x,c)\n"
"			end\n"
"		end\n"
"	end\n"
"end\n"
"\n"
"__spr=spr\n"
"function spr(n, x, y, w, h, flip_x, flip_y)\n"
"	x = x or 0\n"
"	y = y or 0\n"
"	w = w or 1\n"
"	h = h or 1\n"
"	flip_x = flip_x or false\n"
"	flip_y = flip_y or false\n"
"	local flip = 0\n"
"	if flip_x then flip = flip + 1 end\n"
"	if flip_y then flip = flip + 2 end\n"
"	local colorkey = {}\n"
"	for color_index, is_transparent in ipairs(__p8_ctrans) do\n"
"		if is_transparent then\n"
"			table.insert(colorkey, color_index - 1) -- TIC-80 uses 0-based colors\n"
"		end\n"
"	end\n"
"	__spr(n, x, y, colorkey, 1, flip, 0, w, h)\n"
"end\n"
"\n"
"__map=map\n"
"function map(cel_x,cel_y,sx,sy,cel_w,cel_h)\n"
" sx,sy=__p8_coord(sx,sy)\n"
" local cel\n"
"	for cy=0,cel_h-1 do\n"
"	 for cx=0,cel_w-1 do\n"
"		 cel=mget(cx+cel_x,cy+cel_y)\n"
"			spr(cel,sx+cx*8,sy+cy*8)\n"
"		end\n"
"	end\n"
"	\n"
/*"	--__map(cel_x,cel_y,cel_w,cel_h,sx,sy,__p8_ctrans)\n"*/
"end\n"
/*
"mapdraw=map\n"
"\n"
*/
"function sset(x,y,c) \n"
" x,y=math.floor(x),math.floor(y)\n"
"	local addr=0x8000+64*(math.floor(x/8)+math.floor(y/8)*16)\n"
"	poke4(addr+(y%8)*8+x%8,c)\n"
"end\n"
"\n"
"function sget(x,y)\n"
" x,y=math.floor(x),math.floor(y)\n"
" local addr=0x8000+64*(math.floor(x/8)+math.floor(y/8)*16)\n"
"	return peek4(addr+(y%8)*8+x%8)\n"
"end\n"
"\n"
"function flip()\n"
/*" --do nothing\n"*/
"end\n"
"\n"
/*"--sprite flags\n"*/
"function fset(n,f,v)\n"
"	if f>7 then\n"
"	 __p8_sflags[n+1]=f\n"
"	else	 \n"
"	 local flags=__p8_sflags[n+1]\n"
"	 if v then\n"
"	  flags=flags|(1<<f)\n"
"		else\n"
"		 flags=flags&~(1<<f)\n"
"		end\n"
"	 __p8_sflags[n+1]=flags	\n"
"	end\n"
"end\n"
"\n"
"function fget(n,f)\n"
" f=f or -1\n"
"	if f<0 then\n"
"	 return __p8_sflags[n+1]\n"
"	end\n"
"	local flags=__p8_sflags[n+1]\n"
"	if flags&(1<<f)>0 then return true end\n"
"	return false\n"
"end\n"
"\n"
/*"--input\n"*/
"pico8ButtonMap = {}\n"
"pico8ButtonMap[1] = 2 -- 0 left\n"
"pico8ButtonMap[2] = 3 -- 1 right\n"
"pico8ButtonMap[3] = 0 -- 2 up\n"
"pico8ButtonMap[4] = 1 -- 3 down\n"
"pico8ButtonMap[5] = 4 -- 4 o\n"
"pico8ButtonMap[6] = 5 -- 5 x\n"
"pico8ButtonMap[7] = 6 -- 6 start\n"
"pico8ButtonMap[8] = 7 -- 7 Doesn\'t exist\n"
"function pico8ButtonToTic80(i, p)\n"
"	if p == nil then\n"
"		p = 0\n"
"	end\n"
"	return p * 8 + pico8ButtonMap[i + 1]\n"
"end\n"
"__btn = btn\n"
"function btn(i, p)\n"
"	return __btn(pico8ButtonToTic80(i, p))\n"
"end\n"
"__btnp = btnp\n"
"function btnp(i, p)\n"
"	return __btnp(pico8ButtonToTic80(i, p))\n"
"end\n"
"\n"
/*"-- TIC function to call pico-8 callbacks.\n"*/
"__updateTick = true\n"
"__initalized = false\n"
"function TIC()\n"
"	-- Initialize\n"
"	if __initalized == false then\n"
/*"		PICO8_PALETTE()\n"*/
"		if _init ~= nil then\n"
"			_init()\n"
"		end\n"
"		__initalized = true\n"
"	end\n"
"\n"
/*"	-- Update and Draw\n"*/
"	if _update60 ~= nil then -- 60 FPS\n"
"		_update60()\n"
"		if _draw ~= nil then _draw() end\n"
"	elseif _update ~= nil then -- 30 FPS\n"
"		if __updateTick then\n"
"			_update()\n"
"			if _draw ~= nil then _draw() end\n"
"		end\n"
"		__updateTick = not __updateTick\n"
"	end\n"
"end\n"
"\n"
"-- Add pico-8 cart below!\n";
