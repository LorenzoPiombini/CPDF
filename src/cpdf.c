#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <assert.h>
#include <stdint.h>
#include "cpdf.h"

/*used to display error messages, it makes it naicier*/
static char prog[] = "cpdf";

/* key word used for searching the buffer*/
#define XOBJ "/XObject"
#define XOBJ_L 8
#define STREAM_DECODE "/Filter"
#define STREAM_START "stream"
#define STREAM_LENGTH "/Length"
#define STREAM_END   "endstream"
#define ASCII "/ASCII85Decode"
#define FLATE "/FlateDecode"
#define PROD "/Producer"
#define PROD_L 9
#define FONT "/Font"
#define FONT_L 5
#define OBJ "obj"
#define OBJ_L 3 
#define ENDOBJ "endobj"
#define ENDOBJ_L 6
#define UNI "/ToUnicode"
#define UNI_L 10
#define ENDSP "endcodespacerange"
#define ENDSP_L 17
#define BFRANGE "endbfrange"
#define BFRANGE_L 10
#define BEGIN_TEXT "BT"
#define BEGIN_TEXT_L 2



#define UNIASC "unicode.conf"




#define STD_COL 80
#define STRING_MAX 500
#define REF_MAX 50
#define MAX_PAIRS 200

/*structs rappresenting PDF elements*/
struct map{
	short int key;
	unsigned char ascii_char;  
};

struct CMAP{
	struct map pairs[MAX_PAIRS];  
	int size;
};

struct XObject{
	char name[STRING_MAX];
	char ref[REF_MAX];
};

struct XObj_array {
	struct XObject xobj;
};

struct Font{
	char name[STRING_MAX];
	char ref[REF_MAX];
	char uni_ref[REF_MAX];
	struct CMAP cmap;
};

struct Font_array{
	struct Font font;
};


/* internal  pointer used for resume stream search on the PDF buffer*/
static char *resume_point = NULL;

/*internal array to store PDF's Xobjects and Font data*/
static struct XObj_array *xobj_a = NULL;
static struct Font_array *fonts = NULL;

static void *mem_w(const void *haystack, size_t hays_l, const void *needle, size_t ndle_l);
static void *find_start_stream(void *buffer, off_t size, unsigned short *filters);
static void *find_end_stream(void * buffer, char *stream_start, off_t size);
static void decodeASCII85(char *encod, char *decode);
static int pdf_producer(char *buffer, size_t size);
static int guard_tok(const char *src, const char delim, size_t size);
static int load_unicode_ascii_remap(long unicode);
static int write_unknown_unicode(char *unicode);

/*functions for ExpertPdf producer */
static int __expert_find_XObj(char *buffer, size_t size);
static int __expert_find_fonts_info(char *buffer, size_t size);
static int __expert_find_UNIobj(char *buffer, size_t size);
static int __expert_find_CMAP(char *buffer, size_t size);
static int __expert_translate_CMAP(char *buffer, uLongf size);


/*debug functions*/
static int firts_index(const char *src,const char c, uLongf size);

FILE *open_pdf(const char* file_name, off_t *size)
{
	FILE *fp = fopen(file_name,"rb");
	if(!fp){
		fprintf(stderr,"can't open file '%s'.\n",file_name);
		return NULL;
	}

	fseek(fp,0,SEEK_END);
	*size = ftell(fp);
	rewind(fp);
	
	return fp;
}


unsigned char *read_pdf(FILE *fp, off_t size)
{
	unsigned char *buffer = malloc(size);
	if(!buffer) {
		perror("malloc failed");
		return NULL;
	}
	
	if((off_t)fread(buffer,1,size,fp) != size){
		perror("fread failed");
		free(buffer);
		fclose(fp);
		return NULL;
	}

	fclose(fp);
	return buffer;
}


void free_PDF()
{
	free(fonts);
	free(xobj_a);
}

int search_PDF(char *buffer, size_t size)
{
	int res = 0;
	if((res = pdf_producer(buffer,size)) == NOPROD) return res;

	if(res == EPROD) return res;

	switch(res){
	case EXPERT:
 	 	if(__expert_find_XObj(buffer,size) == 0){
			/*find data regarding the Xobject*/
			/*Fonts*/
			if(__expert_find_fonts_info(buffer,size) == -1) return -1;
			
			int result = 0;
			if((result = __expert_find_UNIobj(buffer,size)) == -1 ) return -1;

			if(result == NOUNI){
				/*TODO :
				 * come up with the course of actions
				 * if there is no unicode map in the pdf 
				 * */
			} else {
				if(__expert_find_CMAP(buffer,size) == -1)
					return -1;
			}
		}
		break;
	default:
		break;
	}


	/**/
	
	return 0;

}
int unzip_stream(char *buffer, size_t size, char **unzip_result)
{
	if(resume_point)
		if(resume_point[0] == '\0')
			return END;

	unsigned short filters = 0;
	char *start_stream = NULL;
	char *end_stream = NULL;
	if(!resume_point){
		start_stream = find_start_stream(buffer,size,&filters);
		if(!start_stream) return -1;
		end_stream = find_end_stream(buffer,start_stream,size);
		if(!end_stream) return -1;
	}else {
		start_stream = find_start_stream(resume_point,size - (resume_point - buffer),&filters); 
		if(!start_stream) return -1;
		end_stream = find_end_stream(resume_point,start_stream,size - (resume_point - buffer));
		if(!end_stream) return -1;
	}

	size_t compress_l = end_stream-start_stream -1;

	char cpy[compress_l+1];
	memset(cpy,0,compress_l+1);
	strncpy(cpy,start_stream,compress_l);
	cpy[compress_l] = '\0';
	
	if(FLDEC == (filters & FLDEC)){
		uLongf decompressed_len = compress_l * 4;
		*unzip_result = malloc(decompressed_len);
		if(!(*unzip_result)){
			perror("malloc()");
			return -1;
		}
		memset(*unzip_result,0,decompressed_len);

		char decode[compress_l];
		size_t decode_l = 0l;
		memset(decode,0,compress_l);
		if(ASC85 & (filters & ASC85)) {
			decodeASCII85(cpy,decode);
			decode_l = strlen(decode);
			/*printf("\n\n%s\n\n",decode);*/
			int res = uncompress(((Bytef*)*unzip_result), &decompressed_len,
					(Bytef *)decode,decode_l);

			if (res != Z_OK) {
				fprintf(stderr, "zlib decompression failed! Code: %d\n", res);
				free(*unzip_result);
				return -1;
			}

			return 0;
		}

	
		int res = uncompress(((Bytef*)*unzip_result), &decompressed_len,(Bytef *)start_stream, compress_l);
	
		if (res != Z_OK) {
			switch(res){
			case Z_BUF_ERROR:
			{
				int count = 5; 
				while(res == Z_BUF_ERROR){
					free(*unzip_result);
					*unzip_result = NULL;
					decompressed_len = compress_l * count;
					*unzip_result = malloc(decompressed_len);
					if(!(*unzip_result)){
						perror("malloc()");
						return -1;
					}

					res = uncompress(((Bytef*)*unzip_result), &decompressed_len,(Bytef *)start_stream, compress_l);
					count++;
				}
				if(res != Z_OK){
					fprintf(stderr, "zlib decompression failed after retry! Code: %d\n", res);
					free(*unzip_result);
					return -1;
				}

				/*translate the unzip_result with CMAP*/
				__expert_translate_CMAP(*unzip_result,decompressed_len);
				break;
			}
			default:
				fprintf(stderr, "zlib decompression failed! Code: %d\n", res);
				free(*unzip_result);
				return -1;
			}
		}
		
		return 0;
	}

	printf("Filter  not handled");
	return -1;
}

/*static functions declarations */




static void *mem_w(const void *haystack, size_t hays_l, const void *needle, size_t ndle_l)
{
	if(ndle_l == 0 || hays_l == 0 ) return NULL;

	for(size_t i = 0; i <= hays_l - ndle_l; i ++){
		if(memcmp((char *)haystack + i,needle, ndle_l) == 0){
			return (void*)((char*)haystack + i );
		}
	}

	return NULL;
}

static int pdf_producer(char *buffer, size_t size)
{
	char *producer = mem_w(buffer,size,PROD,PROD_L);
	if(!producer){
		/*error*/
		fprintf(stderr,"producer not found.\n");
		return EPROD;
	}

	if(strstr(producer,MACpdf) != NULL) return MAC;
	if(strstr(producer,ACROpdf) != NULL) return ACRO;
	if(strstr(producer,EXPERTpdf) != NULL) return EXPERT;

	return NOPROD;
}

static int __expert_find_XObj(char *buffer, size_t size)
{
	/*if is NULL initialized an array of struct Xobj_array*/
	if(!xobj_a){
		errno = 0;
		xobj_a = calloc(BASE_ARR_L,sizeof(struct XObj_array));
		if(!xobj_a){
			perror("calloc");
			if(errno = ENOMEM)
				return ENOMEM;
			else 
				return -1;
		}
	}

	/*look for /Xobject in the PDF buffer*/
	char *Xobject = mem_w(buffer,size,XOBJ,XOBJ_L); 
	if(!Xobject)
		return XOBJNONE;
	

	
	
	/* 
	 * this flag is used to continue the do while loop
	 * if the same Xobject has been found
	 * */

	unsigned char cont = 0;
	do{
		Xobject += XOBJ_L;
		
		if(*Xobject == '\r' || *Xobject == '\n')
			continue;

		char *start = Xobject;
		int obj_count = 0;
		while(*Xobject != '>') {
			if(*Xobject == '/')
				obj_count++;
			Xobject++;
		}

		Xobject = start;

		while(isspace(*Xobject) || *Xobject == '<'){
			Xobject++;
		}

		size_t l = strlen(Xobject) + 1;
		char buf_cpy[l];
		memset(buf_cpy,0,l);
		strncpy(buf_cpy,Xobject,l);

		while(obj_count > 0){
			char *t = strtok(buf_cpy," ");
			size_t token_l = strlen(t);

			if(xobj_a[0].xobj.name[0] == '\0'){
				memset(xobj_a[0].xobj.name,0,STRING_MAX);
				strncpy(xobj_a[0].xobj.name,t,token_l);
				char *reference = &buf_cpy[token_l+1];

				char *ref = strtok(reference,"R");
				size_t ref_l = strlen(ref);
				strncpy(xobj_a[0].xobj.ref,ref,ref_l);
				strncat(xobj_a[0].xobj.ref,OBJ,OBJ_L+1);

				/* regenerate buf_cpy to the new position*/
				size_t temp_l = strlen(&buf_cpy[token_l + 1 +ref_l +1]);
				char temp[temp_l];
				memset(temp,0,temp_l);
				strncpy(temp,&buf_cpy[token_l + 1 + ref_l +1],temp_l);
				memset(buf_cpy,0,l);
				strncpy(buf_cpy,temp,temp_l);
				obj_count--;
			} else {
				int i = 0;
				for(i = 0; i < BASE_ARR_L; i++){
					if(strncmp(xobj_a[i].xobj.name,t,token_l) == 0){
						cont = 1;
						break;
					}

					if(xobj_a[i].xobj.name[0] == '\0')
						break;

				}

				if(cont){
					size_t move = strlen(xobj_a[i].xobj.name) + strlen(xobj_a[i].xobj.ref) + 2;
					char temp[l];
					memset(temp,0,l);
					strncpy(temp,&buf_cpy[move],l);
					memset(buf_cpy,0,l);
					strncpy(buf_cpy,temp,l);
					obj_count--;
					continue;
				}

				memset(xobj_a[i].xobj.name,0,STRING_MAX);
				strncpy(xobj_a[i].xobj.name,t,token_l);
				char *reference = &buf_cpy[token_l+1];
				char *ref = strtok(reference,"R");
				size_t ref_l = strlen(ref);
				strncpy(xobj_a[i].xobj.ref,ref,ref_l);
				strncat(xobj_a[i].xobj.ref,OBJ,OBJ_L+1);

				/* regenerate buf_cpy to the new position*/
				size_t temp_l = strlen(&buf_cpy[token_l + 1 +ref_l +1]);
				char temp[temp_l];
				memset(temp,0,temp_l);
				strncpy(temp,&buf_cpy[token_l + 1 + ref_l +1],temp_l);
				memset(buf_cpy,0,l);
				strncpy(buf_cpy,temp,temp_l);
				obj_count--;

			}


		}

	}while((Xobject = mem_w(Xobject,size - (Xobject - (char*)buffer),XOBJ,XOBJ_L)));

	return 0;
}


static void *find_start_stream(void *buffer, off_t size, unsigned short *filters)
{
	char *filter = mem_w(buffer,size,STREAM_DECODE,strlen(STREAM_DECODE));
	if(!filter){
		fprintf(stderr,"can't find stream in pdf.\n");
		return NULL;
	}
	
	char cpy_fil[50] = {0};
	strncpy(cpy_fil,filter,49);

	if(strstr(cpy_fil,ASCII) != NULL)
		*filters |= ASC85;

	if(strstr(cpy_fil,FLATE) != NULL)
		*filters |= FLDEC;

	
	char *start_stream = mem_w((void*)&((char*)buffer)[filter-(char*)buffer], 
			size-(filter - (char *)buffer),STREAM_START, strlen(STREAM_START));
	if(!start_stream){
		fprintf(stderr,"can't find stream in pdf.\n");
		return NULL;
	}

	start_stream += strlen(STREAM_START);
	while(*start_stream == '\n' || *start_stream == '\r'){
		start_stream++;
	}

	return (void*)start_stream;
}

static void *find_end_stream(void * buffer, char *stream_start, off_t size)
{
	char *end_stream = mem_w((void*)&((char*)buffer)[stream_start-(char*)buffer],
			size - (stream_start - (char *)buffer),STREAM_END,strlen(STREAM_END)); 

	if(!end_stream){
		fprintf(stderr,"can't find stream in pdf.\n");
		resume_point = "\0";
		return NULL;
	}

	resume_point = end_stream + strlen(STREAM_END);

	return (void*)end_stream;
}

static void decodeASCII85(char *encod, char* decode)
{
	int final = 0;
	int di = 0;
	int n = 0;
	size_t size = strlen(encod);
	for(size_t i = 0, count = 1; i < size; i++,count++){
		if(encod[i] == '~'){
			if(count == 5){
				count = 0;
				while(count < 4){
					switch (count){
					case 0:
					{
						int copy = final;
						int c = (copy >> 24) & 0xff;
						decode[di] = c;
						di++;
						break;
					}
					case 1:
					{
						int copy = final;
						int c = (copy >> 16) & 0xff;
						decode[di] = c;
						di++;
						break;
					}
					case 2:
					{
						int copy = final;
						int c = (copy >> 8) & 0xff;
						decode[di] = c;
						di++;
						break;
					}
					case 3:
					{
						int c = final & 0xff;
						decode[di] = c;
						di++;
						break;
					}
					default:
						break;
					}
					count++;
				}
			}
			break;
		}
		
		if(isspace(encod[i])){
			count--;
			continue;
		}

		if(encod[i] == 'z'){
			count = 0;
			memset(&decode[di],0,4); 
			di += 4;
			continue;
		}

		n = (unsigned char)encod[i] - '!';
		final += n*(int)pow(85,5-count);
		if(count == 5){
			count = 0;
			while(count < 4){
				switch (count){
				case 0:
					int copy = final;
					int c = (copy >> 24) & 0xff;
					decode[di] = c;
					di++;
					break;
				case 1:
				{
					int copy = final;
					int c = (copy >> 16) & 0xff;
					decode[di] = c;
					di++;
					break;
				}
				case 2:
				{
					int copy = final;
					int c = (copy >> 8) & 0xff;
					decode[di] = c;
					di++;
					break;
				}
				case 3:
				{
					int c = final & 0xff;
					decode[di] = c;
					di++;
					break;
				}
				default:
					break;
				}
				count++;
			}

		count = 0;
		final = 0;
		}
		
		if ( size - i <= 5){
			if((count + (size - i)) >= 5)
				continue;


		}
		
	}
	decode[di] = '\0';
}

static int __expert_find_fonts_info(char *buffer, size_t size)
{

	if(!fonts){
		errno = 0;
		fonts = calloc(BASE_ARR_L,sizeof(struct Font_array));
		if(!fonts){
			perror("calloc");
			if(errno == ENOMEM)
				return ENOMEM;
			else
				return -1;
		}
	}

	/*move to the Xobject reference and look at the font */
	for(int i = 0; i < BASE_ARR_L; i++){
		if(xobj_a[i].xobj.name[0] == '\0') return 0;

		char *obj = mem_w(buffer,size,xobj_a[i].xobj.ref,strlen(xobj_a[i].xobj.ref));
		assert(obj != NULL);

		char *fonts_str = mem_w(obj,size-(obj - buffer),FONT,FONT_L);
		if(!fonts_str){
			continue;
		}

		fonts_str += FONT_L;
		while(isspace(*fonts_str) || *fonts_str == '<') fonts_str++;
		
		/*count the font*/
		char *start = fonts_str;
		int font_c = 0;
		while(*fonts_str != '>'){
			if(*fonts_str == '/')
				font_c++;
			fonts_str++;
		}

		fonts_str = start;
		size_t l = strlen(fonts_str)+1;
		char buf_cpy[l];
		memset(buf_cpy,0,l);
		strncpy(buf_cpy,fonts_str,l);

		unsigned char cont = 0;
		while(font_c > 0){
			
			char *t = strtok(buf_cpy," ");
			size_t token_l = strlen(t);

			if(fonts[0].font.name[0] == '\0'){
				while(isspace(*t)) t++;
				strncpy(fonts[0].font.name,t,token_l);
				char *reference = &buf_cpy[token_l+1];
				
				char *ref = strtok(reference,"R");
				size_t ref_l = strlen(ref);
				strncpy(fonts[0].font.ref,ref,ref_l);
				strncat(fonts[0].font.ref,OBJ,OBJ_L+1);
				/* regenerate buf_cpy to the new position*/
				size_t temp_l = strlen(&buf_cpy[token_l + 1 +ref_l +1]);
				char temp[temp_l];
				memset(temp,0,temp_l);
				strncpy(temp,&buf_cpy[token_l + 1 + ref_l +1],temp_l);
				memset(buf_cpy,0,l);
				strncpy(buf_cpy,temp,temp_l);
				font_c--;
			} else {
				int i;
				for(i = 0; i < BASE_ARR_L;i++){
					if(strncmp(fonts[i].font.name,t,token_l) == 0){
						cont = 1;
						break;
					}

					if(fonts[i].font.name[0] == '\0')
						break;
				}

				if(cont){
					size_t move = strlen(fonts[i].font.name) + strlen(fonts[i].font.ref) + 2;
					char temp[l];
					memset(temp,0,l);
					strncpy(temp,&buf_cpy[move],l);
					memset(buf_cpy,0,l);
					strncpy(buf_cpy,temp,l);
					font_c--;
					cont = 0;
					continue;
				}

				while(isspace(*t)) t++;

				strncpy(fonts[i].font.name,t,token_l);
				char *reference = &buf_cpy[token_l+1];
				
				char *ref = strtok(reference,"R");
				size_t ref_l = strlen(ref);
				strncpy(fonts[i].font.ref,ref,ref_l);
				strncat(fonts[i].font.ref,OBJ,OBJ_L+1);
				/* regenerate buf_cpy to the new position*/
				size_t temp_l = strlen(&buf_cpy[token_l + 1 +ref_l +1]);
				char temp[temp_l];
				memset(temp,0,temp_l);
				strncpy(temp,&buf_cpy[token_l + 1 + ref_l +1],temp_l);
				memset(buf_cpy,0,l);
				strncpy(buf_cpy,temp,temp_l);
				font_c--;
			}

		}

	}

	return 0;
}


static int __expert_find_UNIobj(char *buffer, size_t size)
{
	int found = 0;
	for(int i = 0; i < BASE_ARR_L; i++){
		if(fonts[i].font.name[0] == '\0') {
			if (found) return 0;

			return NOUNI;
		}
		
		char *obj = mem_w(buffer,size,fonts[i].font.ref,strlen(fonts[i].font.ref));
		assert(obj != NULL);
		
		char *end_obj = mem_w(obj,size -(obj - buffer),ENDOBJ,ENDOBJ_L);
		assert(end_obj != NULL);
		
		size_t buf_l = end_obj - obj +1;

		char *unicode = mem_w(obj,buf_l,UNI,UNI_L);
		if(!unicode) continue;

		found++;
		unicode += UNI_L + 1;
		
		size_t l = strlen(unicode) + 1;
		char cpy[l];
		memset(cpy,0,l);
		strncpy(cpy,unicode,l);
		char *t = strtok(cpy,"R");
		
		strncpy(fonts[i].font.uni_ref,t,strlen(t));
		strncat(fonts[i].font.uni_ref,OBJ,OBJ_L+1);

	}

	if(found) return 0;

	return -1;
}


static int __expert_find_CMAP(char *buffer, size_t size)
{
	for(int i = 0; i < BASE_ARR_L; i++){
		if(fonts[i].font.name[0] == '\0') return 0;

		if(fonts[i].font.uni_ref[0] == '\0') continue;

		char *obj = mem_w(buffer, size, fonts[i].font.uni_ref,strlen(fonts[i].font.uni_ref));
		assert(obj != NULL);

		char *endcodespace = mem_w(obj,size -(obj - buffer),ENDSP,ENDSP_L);
		if (!endcodespace){
			fprintf(stderr,"(%s): can't find CMAP.\n",prog);
			return -1;
		}

		endcodespace += ENDSP_L;
		while(isspace(*endcodespace)) endcodespace++;
		
		size_t l = strlen(endcodespace) +1;
		char cpy[l];
		memset(cpy,0,l);

		strncpy(cpy,endcodespace,l);

		char *t = strtok(cpy," ");
		char *endptr;
		long n = strtol(t,&endptr,10);
		if(*endptr == '\0')
			fonts[i].font.cmap.size = (int)n;
		
		while(*endcodespace != '<') endcodespace++;
		

		char *endrange = mem_w(endcodespace,size - (endcodespace - buffer),BFRANGE,BFRANGE_L);

		l = endrange - endcodespace +1;
		char cpy_map[l];
		memset(cpy_map,0,l);
		strncpy(cpy_map,endcodespace,l);
		int index = 0;
		char *p = &cpy_map[0];
		t = strtok(p,">");
		
		repeat:
		while(isspace(*t) || *t == '<') t++;
		n = strtol(t,&endptr,16);
		if(*endptr == '\0')
			fonts[i].font.cmap.pairs[index].key = (short int)n;
		
			
		 t = strtok(NULL,">");
		 t = strtok(NULL,">");
		
		 if(!t) continue;

		 n = strtol(&t[1],&endptr,16);
		 if(*endptr == '\0'){
			 if(n > 0x007f){
				
				int value = load_unicode_ascii_remap(n);
				if(value != 0x003f){
			 		fonts[i].font.cmap.pairs[index].ascii_char = (unsigned char)value;
			 	}else{
			 		fonts[i].font.cmap.pairs[index].ascii_char = (unsigned char)value;
					assert(write_unknown_unicode(&t[1]) != -1);
				}
			 }else{ 
			 	fonts[i].font.cmap.pairs[index].ascii_char = (unsigned char) n;
			 }
		 }

		index++;
		if(guard_tok(cpy_map,'>',l) != 0)
			while((t = strtok(NULL,">"))) goto repeat;

	}

	return 0;
}

static int firts_index(const char *src,const char c, uLongf size)
{
	for(uLongf i = 0; i < size; i ++)
		if(src[i] == c) return i;

	return 0;
}
static int guard_tok(const char *src, const char delim, size_t size)
{
	for(size_t i = 0; i < size; i++)
		if(src[i] == delim) return i;

	return 0;
}

static int __expert_translate_CMAP(char *buffer, uLongf size)
{
	char translate[5000] = {0};
	int index = 0;

		

	char *BT = mem_w(buffer,size,BEGIN_TEXT,BEGIN_TEXT_L);
	
	do{	

		char *Tj = mem_w(BT,size-(BT-buffer),"Tj",2);

		/*set tj to the end of the text drawing*/
		while(*Tj != ')') Tj--;

	        BT += 2;
		
		while(isspace(*BT)) BT++;

		/*select the Font*/
		

		int font_index = 0;
		for(int i = 0; i < BASE_ARR_L; i++ ){
			if(fonts[i].font.name[0] == '\0') break;
				
			if(strncmp(fonts[i].font.name,BT,strlen(fonts[i].font.name)) == 0){
				font_index = i;
				break;
			}

		}

		
		while(*BT != '(') BT++;
		
		
		BT += 1;
		for(; BT != Tj; BT++) {

			if(*BT == '\r' || *BT == '\n')
				continue;
		
			unsigned char b1 = (unsigned char)*BT;
			uint16_t key = b1;
			int count = 0;
			
			if (key == 0) continue;

			for(int j = 0; j < fonts[font_index].font.cmap.size; j++){
				if(fonts[font_index].font.cmap.pairs[j].key == key) {
					if(index < 5000){
						translate[index] = (char)fonts[font_index].font.cmap.pairs[j].ascii_char;
						index++;
						count++;
						break;
					}
				}


			}
		}
		
		if(index > 5000) break;
	}while((BT = mem_w(BT,size -(BT-buffer),BEGIN_TEXT,BEGIN_TEXT_L)));

	printf("%s\n",translate);
	return 0;
}

static int load_unicode_ascii_remap(long unicode)
{
	FILE *fp = fopen(UNIASC,"r");
	if(!fp){
		fprintf(stderr,"(%s): can't open file '%s'\n",prog,UNIASC);
		return -1;
	}
		
	char line[STD_COL]= {0};
	
	long n = 0;
	while(fgets(line,STD_COL,fp)){

		size_t l = strlen(line);
		line[l-1] = '\0';/* delate '\n' */
		int index = firts_index(line,':',(uLongf)l);
		char cpy[l];
		memset(cpy,0,l);
		strncpy(cpy,line,&line[index] - &line[0]);

		char *endptr;
		n = strtol(cpy,&endptr,16);
		if(*endptr == '\0')
			if(n == unicode){

				n = strtol(&line[index + 1],&endptr,16);
				if(*endptr == '\0'){
					fclose(fp);
					return n;
				}
			}

	}

	
	fclose(fp);
	n = (long) '?';
	return (int)n;
}
static int write_unknown_unicode(char *unicode)
{
	FILE *fp = fopen(UNIASC,"a+");
	if(!fp){
		fprintf(stderr,"(%s): can't open file '%s'\n",prog,UNIASC);
		return -1;
	}

	rewind(fp);

	char line[STD_COL]= {0};
	
	while(fgets(line,STD_COL,fp)){
		if(strstr(line,unicode) != NULL){
			fclose(fp);
			return UNIEXS;
		}
	}

	memset(line,0,STD_COL);
	
	strncpy(line,unicode,strlen(unicode));
	strncat(line,":?\n",4);
	
	fputs(line,fp);
	fclose(fp);
	return 0;

}
