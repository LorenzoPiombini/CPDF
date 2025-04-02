#ifndef _CPDF_H
#define _CPDF_H

#define BASE_ARR_L 10



/*errors*/
#define END 10 /*no more stream in the pdf file */
#define NOPROD 11 /*producer not supported */
#define XOBJNONE 12 /*PDF with no Xobject */
#define EPROD 13 /*producer not found */
#define NOUNI 14 /*no CMAP unicode for the fonts in the PDF */
#define UNIEXS 15 /*unicode already in the file */


/*MASKS value*/
#define ASC85 1
#define FLDEC 2





/*PDF producers */
#define MACpdf "Quartz PDFContext"
#define EXPERTpdf "ExpertPdf"
#define ACROpdf "Acrobat PDFWriter"

enum Producers{
	MAC,
	EXPERT,
	ACRO
};




FILE *open_pdf(const char* file_name, off_t *size);
unsigned char *read_pdf(FILE *fp, off_t size);
int search_PDF(char *buffer, size_t size);
void free_PDF();
int unzip_stream(char *buffer, size_t size, char **unzip_result);


#endif
