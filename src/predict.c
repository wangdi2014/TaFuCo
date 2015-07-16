/*--------------------------------------------------------------------*/
/* predict.c                                                          */
/* Author: Rongxin Fang                                               */
/* E-mail: r3fang@ucsd.edu                                            */
/* Predict Gene Fusion by given fastq files.                          */
/*--------------------------------------------------------------------*/
#include <stdio.h>  
#include <stdlib.h>  
#include <string.h> 
#include <zlib.h>  
#include <assert.h>
#include <math.h>
#include "uthash.h"
#include "kseq.h"
#include "common.h"
#include "kmer_uthash.h"
#include "fasta_uthash.h"
#include "BAG_uthash.h"
KSEQ_INIT(gzFile, gzread);

/*
 * various macros
 */
#define PR_NOROOM					-1 /*no memory to allocate new queue*/
#define PR_SHORTREAD				-2 /*read shorter than k*/
#define PR_UNMATCHED_READPAIR		-3 /*read shorter than k*/

/*
 * error handling
 * all errors are returned as an integer code, and a string
 * amplifying the error is saved in here; this can then be
 * printed
 */
char pr_errbuf[256] = "no error";	/* the error message buffer */
									/* macros to fill it */
#define ERRBUF(str)	(void) strncpy(pr_errbuf, str, sizeof(pr_errbuf))
#define ERRBUF2(str,n)		(void) sprintf(pr_errbuf, str, n)
#define ERRBUF3(str,n,m)	(void) sprintf(pr_errbuf, str, n, m)
/*--------------------------------------------------------------------*/
/*Global paramters.*/

static const char *pcPgmName="predict.c";
static struct kmer_uthash *KMER_HT = NULL;
static struct fasta_uthash *FASTA_HT = NULL;
static struct BAG_uthash *BAG_HT = NULL;

int predict_main(char *fasta_file, char *fq_file1, char *fq_file2);
char* find_next_MEKM(char *, int, int, int);
size_t find_all_MEKMs(char **, char* , int, int);

/*--------------------------------------------------------------------*/

/* Find next Maximal Extended Kmer Match (MEKM) on _read at pos_read. */
char* 
find_next_MEKM(char *_read, int pos_read, int k, int min_match){
	/* copy a part of string */
	char* buff;
	if((buff = malloc(k * sizeof(char)))==NULL){
		ERRBUF("find_next_MEKM: malloc: no more memory");
		return NULL;
	}
	strncpy(buff, _read + pos_read, k);
	buff[k] = '\0';	
	if(buff == NULL || strlen(buff) != k){
		return NULL;
	}
	/*------------------------------------------------------------*/
	struct kmer_uthash *s_kmer = find_kmer(buff, KMER_HT);
	if(s_kmer==NULL){
		return NULL;
	}	
	char **matches;
	if((matches = malloc(s_kmer->count * sizeof(char*)))==NULL){
		ERRBUF("find_next_MEKM: malloc: no more memory");
		return NULL;
	}
	int  *matches_len;
	if((matches_len = malloc(s_kmer->count * sizeof(int)))==NULL){
		ERRBUF("find_next_MEKM: malloc: no more memory");
		return NULL;
	}	
	/*------------------------------------------------------------*/
	int i = 0;
	int num = 0;
	for(i=0; i<s_kmer->count; i++){		
		int _pos_exon; // position on exon
		char *exon = pos_parser(s_kmer->pos[i], &_pos_exon);
		/* error report*/
		if(exon == NULL || _pos_exon<0){
			return NULL;
		}
		
		struct fasta_uthash *s_fasta = find_fasta(exon, FASTA_HT);
		if(s_fasta == NULL){
			return NULL;
		}		
		int m = 0;
		/* extending kmer to find MPM */
		while(*(s_fasta->seq + m + _pos_exon) == *(_read+ pos_read + m)){
			m ++;
		}
		if(m >= k){
			matches_len[num] = m;
			matches[num] = exon;
			num ++;		
		}
	}
	/*------------------------------------------------------------*/	
	char *res_exon = NULL;
	if(num > 0){
		int max_len = max_of_int_array(matches_len, num);
		if(max_len > min_match){
			size_t max_num = 0;
			size_t count;
			int max_ind;
			for(count=0; count < num; count++){
				if(matches_len[count] == max_len){
					max_num ++;
					max_ind = count;
				}
			}
			res_exon = matches[max_ind];
		}
	}
	if(matches_len != NULL){free(matches_len);}	
	if(matches != NULL){free(matches);}	
	if(buff != NULL){free(buff);}
	return(res_exon);
}
/*--------------------------------------------------------------------*/

/* Find all Maximal Extended Kmer Matchs (MEKMs) on _read.            */
size_t 
find_all_MEKMs(char **hits, char* _read, int _k, int min_match){
	assert(hits != NULL);
	int _read_pos = 0;
	size_t _i = 0; 
	while(_read_pos<(strlen(_read)-_k-1)){
		char* _exon = find_next_MEKM(_read, _read_pos, _k, min_match);
		_read_pos += 1;
		if (_exon != NULL){
			hits[_i] = _exon;
			_i++;
		}
	}
	return _i;
}

/* Construct breakend associated graph by given fq files.             */
struct BAG_uthash * 
construct_BAG(char *fq_file1, char *fq_file2, int _k, int min_match){
	struct BAG_uthash *graph_ht = NULL;
	gzFile fp1, fp2;
	kseq_t *seq1, *seq2;
	int l1, l2;
	fp1 = gzopen(fq_file1, "r");
	fp2 = gzopen(fq_file2, "r");
	seq1 = kseq_init(fp1);
	seq2 = kseq_init(fp2);
	while ((l1 = kseq_read(seq1)) >= 0 && (l2 = kseq_read(seq2)) >= 0 ) {
		char *_read1 = rev_com(seq1->seq.s);
		char *_read2 = seq2->seq.s;
		
		if(strcmp(seq1->name.s, seq2->name.s) != 0){
			ERRBUF("construct_BAG: read pair not matched");
			return NULL;
		}			
		
		if(_read1 == NULL || _read2 == NULL)
			continue;    
		
		char **hits1, **hits2;
		if((hits1 = malloc(strlen(_read1) * sizeof(char*)))==NULL){
			ERRBUF("construct_BAG: malloc: no more memory");
			return NULL;
		}
		if((hits2 = malloc(strlen(_read2) * sizeof(char*)))==NULL){
			ERRBUF("construct_BAG: malloc: no more memory");
			return NULL;
		}
		
		if(strlen(_read1) < _k || strlen(_read2) < _k){
			continue;
		}
		size_t num1, num2;		
		num1 = find_all_MEKMs(hits1, _read1, _k, min_match);
		num2 = find_all_MEKMs(hits2, _read2, _k, min_match);
		//// get uniq elements in hits1
		char** hits_uniq1 = malloc(num1 * sizeof(char*));  
		char** hits_uniq2 = malloc(num2 * sizeof(char*));  

		if((hits_uniq1 = malloc(num1 * sizeof(char*)))==NULL){
			ERRBUF("construct_BAG: malloc: no more memory");
			return NULL;
		}
		if((hits_uniq2 = malloc(num2 * sizeof(char*)))==NULL){
			ERRBUF("construct_BAG: malloc: no more memory");
			return NULL;
		}

		size_t size1 = set_str_arr(hits1, hits_uniq1, num1);
		size_t size2 = set_str_arr(hits2, hits_uniq2, num2);
		free(hits1);
		free(hits2);
		int i, j;
		for(i=0; i<size1; i++){
			for(j=0; j<size2; j++){
				char *edge_name;
				size_t len1 = strsplit_size(hits_uniq1[i], ".");
				size_t len2 = strsplit_size(hits_uniq2[j], ".");
				char **parts1 = calloc(len1, sizeof(char *));
				char **parts2 = calloc(len2, sizeof(char *));				
				strsplit(hits_uniq1[i], len1, parts1, ".");  
				strsplit(hits_uniq2[j], len2, parts2, ".");  
				 
				if(parts1[0]==NULL || parts2[0]==NULL){
					free(parts1);
					free(parts2);
					continue;
				}
				
				char* gene1 = strdup(parts1[0]);
				char* gene2 = strdup(parts2[0]);
				
				int rc = strcmp(gene1, gene2);
				if(rc<0)
					edge_name = concat(concat(gene1, "_"), gene2);
				if(rc>0)
					edge_name = concat(concat(gene2, "_"), gene1);
				if(rc==0)
					edge_name = NULL;
				
				if(edge_name!=NULL){
					printf("%s\t%s\n", edge_name, concat(concat(_read1, "_"), _read2));
					BAG_uthash_add(&graph_ht, edge_name, concat(concat(_read1, "_"), _read2));					
				}
				if(edge_name!=NULL){free(edge_name);}
				if(parts1!=NULL){free(parts1);}
				if(parts2!=NULL){free(parts2);}
				if(gene1!=NULL){free(gene1);}
				if(gene2!=NULL){free(gene2);}	
			}
		}
		free(hits_uniq1);
		free(hits_uniq2);
	}
	kseq_destroy(seq1);
	kseq_destroy(seq2);	
	gzclose(fp1);
	gzclose(fp2);
	return graph_ht;
}

/*--------------------------------------------------------------------*/

/* main function. */
int main(int argc, char *argv[]) { 
	if (argc != 5) {  
	        fprintf(stderr, "Usage: %s <in.fa> <read_R1.fq> <read_R2.fq> <int k>\n", argv[0]);  
	        return 1;  
	 }
	char *fasta_file = argv[1];
	char *fq_file1 = argv[2];
	char *fq_file2 = argv[3];
	int min_mtch;		
	if (sscanf (argv[4], "%d", &min_mtch)!=1) { printf ("error - not an integer");}
	/* load kmer hash table in the memory */
	assert(fasta_file != NULL);
	assert(fq_file1 != NULL);
	assert(fq_file2 != NULL);
	
	///* load kmer_uthash table */
	char *index_file = concat(fasta_file, ".index");
	if(index_file == NULL)
		return -1;
	
	//printf("loading kmer uthash table ...\n");
	int k;
	KMER_HT = kmer_uthash_load(index_file, &k);	
	if(KMER_HT == NULL){
		fprintf(stderr, "Fail to load kmer_uthash table\n");
		exit(-1);		
	}
	/* MAX_K is defined in common.h */
	if(k > MAX_K){
		fprintf(stderr, "input k(%d) greater than allowed lenght - 100\n", k);
		exit(-1);		
	}		
	/* load fasta_uthash table */
	FASTA_HT = fasta_uthash_load(fasta_file);
	if(FASTA_HT == NULL){
		fprintf(stderr, "Fail to load fasta_uthash table\n");
		exit(-1);		
	}
	BAG_HT = construct_BAG(fq_file1, fq_file2, k, min_mtch);	
	//BAG_uthash_display(BAG_HT);	
	kmer_uthash_destroy(&KMER_HT);	
	fasta_uthash_destroy(&FASTA_HT);	
	BAG_uthash_destroy(&BAG_HT);	
	return 0;
}

