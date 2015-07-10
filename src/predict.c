#include <stdio.h>   /* gets */
#include <stdlib.h>  /* atoi, malloc */
#include <string.h>  /* strcpy */
#include <zlib.h>  
#include "uthash.h"
#include "utlist.h"
#include "utstring.h"
#include "utarray.h"
#include "kseq.h"
#include "common.h"
KSEQ_INIT(gzFile, gzread)  

struct kmer_uthash *load_kmer_htable(char *fname){
	/* load kmer_htable*/
	struct kmer_uthash *htable = NULL;
	gzFile fp;  
	kseq_t *seq;  
	int l;
	fp = gzopen(fname, "r");
	if (fp == NULL) {
	  fprintf(stderr, "Can't open input file %s!\n", fname);
	  exit(1);
	}
	seq = kseq_init(fp); // STEP 3: initialize seq  
	if (seq == NULL){
		return NULL;
	}
	while ((l = kseq_read(seq)) >= 0) { // STEP 4: read sequence 
		/* add kmer */
		char *kmer = seq->name.s;
		int k = strlen(kmer);
		struct kmer_uthash *s;
		s = (struct kmer_uthash*)malloc(sizeof(struct kmer_uthash));
		strncpy(s->kmer, kmer, k);
		s->kmer[k] = '\0'; /* just in case*/
		
		/* add count */
		int count = atoi(seq->comment.s);
		s->count = count;
		
		/* add pos */
		char *pos = seq->seq.s;				
		s->pos = malloc((s->count) * sizeof(char*));
		
		/* split a string by delim */
		char *token;	    
		/* get the first token */
	    token = strtok(pos, "|");				
		/* walk through other tokens */
		int i = 0;
	    while(token != NULL) 
	    {
			s->pos[i] = malloc((strlen(token)+1) * sizeof(char));
			/*duplicate a string*/
			s->pos[i] = strdup(token);
			token = strtok(NULL, "|");
			i ++;
	    }		
		HASH_ADD_STR(htable, kmer, s);
	}	
	gzclose(fp); // STEP 6: close the file handler  
	kseq_destroy(seq);
	return(htable);
}

struct fasta_uthash *fasta_uthash_load(char *fname){
	gzFile fp;
	kseq_t *seq;
	int l;
	struct fasta_uthash *res = NULL;
	fp = gzopen(fname, "r");
	if(fp == NULL){
		return NULL;
	}
	seq = kseq_init(fp);
	while ((l = kseq_read(seq)) >= 0) {
		struct fasta_uthash *s;
		s = (struct fasta_uthash*)malloc(sizeof(struct fasta_uthash));
		/* if we kseq_destroy(seq);, we need to duplicate the string!!!*/
		s->name = strdup(seq->name.s);
		s->seq = strdup(strToUpper(seq->seq.s));
		if(seq->comment.l) s->comment = seq->comment.s;
		HASH_ADD_STR(res, name, s);
	}	
	kseq_destroy(seq);
	gzclose(fp);
	return res;
}

int find_mpm(char *_read, int *pos_read, int k, struct kmer_uthash **kmer_ht, struct fasta_uthash **fasta_ht){
	struct kmer_uthash *s;
	/* copy a part of string */
	char buff[k];
	strncpy(buff, _read + *pos_read, k);
	HASH_FIND_STR(*kmer_ht, buff, s);
	if(s==NULL){
		*pos_read += 1;
	}else{
		int max_len = 0;
		char* max_exon;
		int pos_seq;
		for(int i=0; i< s->count; i++){
			/* str_split to make this simpler*/
			char *token;
			/* get the first token */
			token = strtok(s->pos[i], "_");				
			/* walk through other tokens */
			char* exon = malloc((strlen(token)+1) * sizeof(char));
			/*duplicate a string*/
			exon = strdup(token);
			token = strtok(NULL, "_");
			pos_seq = atoi(token);
			struct fasta_uthash *tmp;
			/* check if the sequence exists in fasta_htable */
			HASH_FIND_STR(*fasta_ht, exon, tmp);
			if(tmp != NULL){
				int m = 0;
				while(*(tmp->seq+m+pos_seq) == *(_read+m)){
					m ++;
				}
				if(m > max_len){
					max_len = m;
					max_exon = exon;
				}
			}
		}
		*pos_read += max_len;
	}
	return 0;
}

int predict_main(char *fasta_file, char *fastq_file, int k){
	/* load kmer hash table in the memory */
	char *index_file = concat(fasta_file, ".index");	
	/* load kmer_uthash table */
	struct kmer_uthash *kmer_ht = load_kmer_htable(index_file);
	/* load fasta_uthash table */
	struct fasta_uthash *fasta_ht = fasta_uthash_load(fasta_file);

	/* starting parsing and processing fastq file*/
	gzFile fp;
	kseq_t *seq;
	int l;
	fp = gzopen(fastq_file, "r");
	seq = kseq_init(fp);
	while ((l = kseq_read(seq)) >= 0) {
		char *_read = seq->seq.s;
		int pos_read = 0;
		find_mpm(_read, &pos_read, k, &kmer_ht, &fasta_ht);
		
		if(pos_read >= k){
			printf("%d, %d\n", pos_read, k);			
		}
		
//		int i = 0;
//		int buff[k];
//		strncpy(buff, end+i, k);
//		struct kmer_uthash *s;
//		HASH_FIND_STR(kmer_ht, buff, s);
//		if(s!=NULL){
//			/* walk through all matched exons */
//			int max_len = 0;
//			char* max_exon;
//			int num;
//			for(int i=0; i< s->count; i++){
//				/* str_split to make this simpler*/
//				char *token;
//				/* get the first token */
//			    token = strtok(s->pos[i], "_");				
//				/* walk through other tokens */
//				char* exon = malloc((strlen(token)+1) * sizeof(char));
//				/*duplicate a string*/
//				exon = strdup(token);
//				token = strtok(NULL, "_");
//				num = atoi(token);
//				printf("%s\t%d\n", exon, num);
//				struct fasta_uthash *tmp;
//				/* check if the sequence exists in fasta_htable */
//				HASH_FIND_STR(fasta_ht, exon, tmp);
//				if(tmp != NULL){
//					int m = 0;
//					while(*(tmp->seq+m+num) == *(end+m)){
//						m ++;
//					}
//					if(m > max_len){
//						max_len = m;
//						max_exon = exon;
//					}
//				}
//			}
//			printf("%d\t%d\t%s\n", num, max_len, max_exon);
//		}
	}
	
	//struct kmer_uthash *s, *tmp;
	//HASH_ITER(hh, kmer_ht, s, tmp) {
	//	if(s->count > 1){
	//		printf("%s\n", s->kmer);
	//		for(int i=0; i<s->count; i++)
	//		printf("%s\n", s->pos[i]);						
	//	}
	//}
	
	kseq_destroy(seq);
	gzclose(fp);
	kmer_uthash_destroy(&kmer_ht);	
	fasta_uthash_destroy(&fasta_ht);	
	return 0;
}

