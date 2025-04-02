#include <stdio.h>
#include <stdlib.h>
#include "cpdf.h"

char prog[] = "cpdf";

int main(int argc, char **argv)
{
	if(argc < 2){
		fprintf(stderr,"(%s): Usage %s <filename or path>.\n",prog,prog);
		return -1;
	}
	
	off_t size = 0;
	FILE *fp = open_pdf(argv[1],&size);
	if(!fp){
		return -1;
	}

	unsigned char *buffer = read_pdf(fp,size);
	
	if(search_PDF((char *)buffer,size) == -1 ){
		free_PDF();
		return -1;
	}

	for(int i = 0; i < 15; i ++){
		char *result = NULL;
		if(unzip_stream((char*)buffer,size,&result) == -1){
			continue;		
		}

	/*	printf("(%s): Pdf text:\n%s\n",prog,(char*)result);*/
		free(result);
	}

	free(buffer);
	free_PDF(); /*free all the allocated memory */
	return 0;
}
