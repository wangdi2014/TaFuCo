/*--------------------------------------------------------------------*/
/* predict.c                                                          */
/* Author: Rongxin Fang                                               */
/* E-mail: r3fang@ucsd.edu                                            */
/* Predict gene fusion between targeted genes.                        */
/*--------------------------------------------------------------------*/

#include "predict.h"
#include "name2fasta.h"

static kmer_t *kmer_index(fasta_t *, int);
static bag_t  *bag_construct(kmer_t *, gene_t **, char*, char*, int, int, int);
static char *concat_exons(char* _read, fasta_t *fa_ht, kmer_t *kmer_ht, int _k, char *gname1, char* gname2, char** ename1, char** ename2, int *junction, int min_kmer_match);
static int find_junction_one_edge(bag_t *eg, fasta_t *fasta_u, opt_t *opt, junction_t **ret);
static int update_junction(junction_t **junc, solution_pair_t **sol_pair, opt_t *opt, char* fuse_name, char* junc_name);
static int gene_order(char* gname1, char* gname2, char* read1, char* read2, kmer_t *kmer_ht, int k, int min_kmer_match);
static junction_t *transcript_construct_no_junc(char* gname1, char *gname2, fasta_t *fasta_ht);
static junction_t *transcript_construct_junc(junction_t *junc_ht, fasta_t *exon_ht);
static inline int find_all_genes(str_ctr **hash, kmer_t *KMER_HT, char* _read, int _k);
static int update_fusion(bag_t **edge, solution_pair_t **res, opt_t *opt);

/*
 * Description:
 *------------
 * index input sequences by kmer hash table

 * Input: 
 *-------
 * fa        - fasta_t hash table contains sequences to be indexed
 * k         - length of kmer

 * Output: 
 *-------
 * kmer_t object that contains kmer and its occurnace positions on input seq.
 */
static kmer_t 
*kmer_index(fasta_t *tb, int k){
	if(tb == NULL || k <= 0 || k > MAX_KMER_LEN) return NULL;
	register char *kmer;
	char *name, *seq;	
	register int i, j;
	kmer_t *ret = NULL;
	fasta_t *fa_cur = NULL;
	for(fa_cur=tb; fa_cur!=NULL; fa_cur=fa_cur->hh.next){
		name = seq = NULL;
		seq = strToUpper(fa_cur->seq);
		name = strdup(fa_cur->name);
		if(seq == NULL || name == NULL || strlen(seq) <= k) continue;
		for(i=0; i < strlen(seq)-k+1; i++){
			kmer = NULL;
			kmer = mycalloc(k+1, char);
			memset(kmer, '\0', k+1);
			strncpy(kmer, seq+i, k);
			kmer[k] = '\0';
			kmer_add(&ret, kmer, name); 
			free(kmer);
		}
		if(seq)    free(seq);
		if(name)   free(name);
	}
	kmer_uniq(&ret);
	return ret;
}


/*
 * Description:
 *------------
 * construct breakend associated graph BAG.

 * Input: 
 *-------
 * kmer_ht            - kmer_t object returned by kmer_index
 * *gene_ht           - gene_t object that will store a gene is supported by # of reads
 * fq1                - fastq file that contains 5' to 3' read
 * fq2                - fastq file that contains the other end of read 
 * min_kmer_matches   - min number kmer matches between a gene and read needed 
 * min_edge_weight    - edges in the graph with weight smaller than min_edge_weight will be deleted
 * k                  - length of kmer
 * Output: 
 *-------
 * BAG_uthash object that contains the graph.
 */
static bag_t
*bag_construct(kmer_t *kmer_ht, gene_t **gene_ht, char* fq1, char* fq2, int min_kmer_matches, int min_edge_weight, int _k){
	if(kmer_ht==NULL || fq1==NULL || fq2==NULL || *gene_ht==NULL) return NULL;
	/* variable declaration */
	bag_t *bag = NULL;
	gzFile fp1, fp2;
	int l1, l2;
	kseq_t *seq1, *seq2;
	int i, num;
	char *_read1, *_read2, *edge_name;
	char **hits;
	str_ctr *s, *gene_counter;
	gene_t *gene_cur;
	/* file check */
	if((fp1 = gzopen(fq1, "r"))==NULL) die("[%s] fail to read fastq files", __func__);
	if((fp2 = gzopen(fq2, "r"))==NULL) die("[%s] fail to read fastq files", __func__);	
	if((seq1 = kseq_init(fp1)) ==NULL)  die("[%s] fail to read fastq files", __func__);
	if((seq2 = kseq_init(fp2)) ==NULL)  die("[%s] fail to read fastq files", __func__);
		
	/* iterate read pair in both fastq files */
	while ((l1 = kseq_read(seq1)) >= 0 && (l2 = kseq_read(seq2)) >= 0){
		_read1 = _read2 = edge_name = NULL;
		gene_counter = NULL;
		hits = NULL;
		_read1 = rev_com(seq1->seq.s); // reverse complement of read1
		_read2 =  strdup(seq2->seq.s);
			
		if(_read1 == NULL || _read2 == NULL){
			if(_read1) free(_read1);
			if(_read2) free(_read2);
		}
		if(strlen(_read1) < _k || strlen(_read2) < _k){
			if(_read1) free(_read1);
			if(_read2) free(_read2);	
		}
		
		find_all_genes(&gene_counter, kmer_ht, _read1, _k);
		find_all_genes(&gene_counter, kmer_ht, _read2, _k);
		
		// count hits of the gene
		int max_hits = -10;
		char *max_gene = NULL;
		
		for(s=gene_counter; s!=NULL; s=s->hh.next){
			if(s->SIZE > max_hits){
				max_hits = s->SIZE; 
				max_gene = strdup(s->KEY);
			}
		}
		
		if(max_hits >= min_kmer_matches*2 && max_gene!=NULL){
			if((gene_cur=find_gene(*gene_ht, max_gene))!=NULL){
				gene_cur->hits++;
			}			
		}
		
		if((num = HASH_COUNT(gene_counter))<2){
			if(_read1)       free(_read1);
			if(_read2)       free(_read2);
			if(gene_counter) str_ctr_destory(&gene_counter);
			continue; 	
		}
		hits = mycalloc(num, char*);
		
		///* filter genes that have matches with kmer less than min_kmer_matches */
		i=0; for(s=gene_counter; s!=NULL; s=s->hh.next){if(s->SIZE >= min_kmer_matches){hits[i++] = strdup(s->KEY);}}

		int m, n; for(m=0; m < i; m++){for(n=m+1; n < i; n++){
				int rc = strcmp(hits[m], hits[n]);
				if(rc<0)  edge_name = concat(concat(hits[m], "_"), hits[n]);
				if(rc>0)  edge_name = concat(concat(hits[n], "_"), hits[m]);
				if(rc==0) edge_name = NULL;
				if(edge_name!=NULL) if(bag_add(&bag, edge_name, seq1->name.s, concat(concat(_read1, "_"), _read2)) != 0) die("BAG_uthash_add fails\n");					
		}}
		
		// clean the mess up
		if(edge_name)    free(edge_name);
		if(hits){
			for(i--;i>0; i--){
				if(hits[i]) free(hits[i]);
		   }
		   free(hits);
		}		
		if(_read1)       free(_read1);
		if(_read2)       free(_read2);
		if(gene_counter) str_ctr_destory(&gene_counter);
	}
	
	// determine gene order by kmer matches
	int order;
	char** gnames;
	bag_t *cur;
	for(cur=bag; cur!=NULL; cur=cur->hh.next){
		order = 0;
		gnames = NULL;
		gnames = strsplit(cur->edge, '_', &num); if(num!=2) continue;
		for(i=0; i<cur->weight; i++){
			hits = NULL;
			hits = strsplit(cur->evidence[i], '_', &num);
			if(num!=2) continue;
			order += gene_order(gnames[0], gnames[1], hits[0], hits[1], kmer_ht, _k, min_kmer_matches);
			if(hits){free(hits[0]); free(hits[1]);}
		}
		if(order > 0){
			cur->gname1 = strdup(gnames[1]); 
			cur->gname2 = strdup(gnames[0]);
		}
		if(order < 0){
			cur->gname1 = strdup(gnames[0]); 
			cur->gname2 = strdup(gnames[1]);
		}
		if(order == 0){HASH_DEL(bag, cur); free(cur);}		
		if(gnames){free(gnames[0]); free(gnames[1]);}
	}
	// clean the mess up
	kseq_destroy(seq1);
	kseq_destroy(seq2);	
	gzclose(fp1);
	gzclose(fp2);
	
	if(bag_uniq(&bag)!=0){
		fprintf(stderr, "[%s] fail to remove duplicate supportive reads \n", __func__);
		return NULL;		
	}
	return bag;
}

/*
 * determine the order of fusion genes.
 * negative means gene1 in front of gene1 from 5'-3'
 */
static int 
gene_order(char* gname1, char* gname2, char* read1, char* read2, kmer_t *kmer_ht, int k, int min_kmer_match){
	if(gname1==NULL || gname2==NULL || read1==NULL || read2==NULL || kmer_ht==NULL) return 0;
	register int i;
	int *gene1 = mycalloc(strlen(read1)+strlen(read2), int);
	int *gene2 = mycalloc(strlen(read1)+strlen(read2), int);
	int gene1_pos = 0;	
	int gene2_pos = 0;	
	int num;
	char* gname_tmp;
	register kmer_t *kmer_cur;
	char *buff = mycalloc(k+1, char);
	for(i=0; i<strlen(read1)-k+1; i++){
		memset(buff, '\0', k+1);
		strncpy(buff, read1 + i, k); buff[k] = '\0';			
		if((kmer_cur=find_kmer(kmer_ht, buff)) == NULL) continue;
		if(kmer_cur->count == 1){  // uniq match
			gname_tmp = strsplit(kmer_cur->seq_names[0], '.', &num)[0];
			if(strcmp(gname_tmp, gname1)==0) gene1[gene1_pos++] = i;
			if(strcmp(gname_tmp, gname2)==0) gene2[gene2_pos++] = i;
		}
	}
	for(i=0; i<strlen(read2)-k+1; i++){
		memset(buff, '\0', k+1);
		strncpy(buff, read2 + i, k); buff[k] = '\0';			
		if((kmer_cur=find_kmer(kmer_ht, buff)) == NULL) continue;
		if(kmer_cur->count == 1){  // uniq match
			gname_tmp = strsplit(kmer_cur->seq_names[0], '.', &num)[0];
			if(strcmp(gname_tmp, gname1)==0) gene1[gene1_pos++] = i+strlen(read1);
			if(strcmp(gname_tmp, gname2)==0) gene2[gene2_pos++] = i+strlen(read1);
		}
	}
	int t = 0;
	if(gene1_pos >= min_kmer_match && gene2_pos >= min_kmer_match){
		for(i=0; i<min(gene1_pos, gene2_pos); i++){
			if(gene1[i] < gene2[i]) t++;
			if(gene1[i] > gene2[i]) t--;
		}
		return t;
	}
	return 0;
}

/* 
 * Find all exons uniquely matched with kmers on _read.          
 * hash     - a hash table count number of matches between _read and every gene
 * _read    - inqury read
 * _k       - kmer length
 */
static inline int
find_all_exons(str_ctr **hash, kmer_t *KMER_HT, char* _read, int _k){
/*--------------------------------------------------------------------*/
	/* check parameters */
	if(_read == NULL || _k < 0) die("find_all_MEKMs: parameter error\n");
/*--------------------------------------------------------------------*/
	/* declare vaiables */
	int _read_pos = 0;
	char* exon = NULL;
	register kmer_t *s_kmer = NULL; 
	char *buff = mycalloc(_k+1, char);
/*--------------------------------------------------------------------*/
	while(_read_pos<(strlen(_read)-_k+1)){
		/* copy a kmer of string */
		memset(buff, '\0', _k+1);
		strncpy(buff, _read + _read_pos, _k); buff[_k] = '\0';	
		if(strlen(buff) != _k) continue;
		/*------------------------------------------------------------*/
		if((s_kmer=find_kmer(KMER_HT, buff)) == NULL){_read_pos++; continue;} // kmer not in table but not an error
		if(s_kmer->count == 1){str_ctr_add(hash, s_kmer->seq_names[0]);}
		_read_pos++;
	}
	if(buff)    free(buff);
	return 0;
}
/*
 * Find all genes uniquely matched with kmers on _read.          
 * hash     - a hash table count number of matches between _read and every gene
 * _read    - inqury read
 * _k       - kmer length
 */
static inline int
find_all_genes(str_ctr **hash, kmer_t *kmer_ht, char* _read, int _k){
	/* check parameters */
	if(_read == NULL || kmer_ht == NULL || _k < 0) die("[%s]: parameter error\n", __func__);
	/* declare vaiables */
	str_ctr *s;
	int _read_pos = 0;
	int num;
	kmer_t *s_kmer = NULL; 
	char buff[_k];
	char** fields = NULL;
	int i;
/*--------------------------------------------------------------------*/
	while(_read_pos<(strlen(_read)-_k+1)){
	//	/* copy a kmer of string */
		strncpy(buff, _read + _read_pos, _k); buff[_k] = '\0';	
		if(strlen(buff) != _k) continue;
		if((s_kmer=find_kmer(kmer_ht, buff)) == NULL){_read_pos++; continue;} // kmer not in table but not an error
		if(s_kmer->count == 1){ // only count the uniq match 
			fields = strsplit(s_kmer->seq_names[0], '.', &num);
			if(num!=2) continue;
			str_ctr_add(hash, fields[0]);
			if(fields) {for(i=0; i<num; i++) free(fields[i]); free(fields);}
		}
		_read_pos++;
	}	
	return 0;
}

static junction_t
*edge_junction_gen(bag_t *eg, fasta_t *fasta_u, kmer_t *kmer_ht, opt_t *opt){
	if(eg==NULL || fasta_u==NULL || opt==NULL) return NULL;
	/* variables */
	int _k = opt->k;
	int num;
	register char *str1, *str2;
	char *ename1, *ename2;
	char *chrom1, *chrom2;
	char* idx = NULL;
	chrom2 = chrom1 = NULL;
	ename1 = ename2 = NULL;
	str1 = str2 = NULL;
	char *gname1, *gname2;
	gname1 = eg->gname1;
	gname2 = eg->gname2;
	if(gname1==NULL || gname2==NULL) return NULL;
	register int i, j;
	register kmer_t *s_kmer; 
	solution_t *sol1, *sol2;           /* alignment solution for read1 and read2 */
	int start1, start2;
	int junc_pos;                               /* position of junction */
	int strlen2;
	char** fields;
	junction_t *m, *n, *ret = NULL;
	
	for(i=0; i<eg->weight; i++){
		fields = NULL;
		fields = strsplit(eg->evidence[i], '_', &num);
		if(num!=2) continue;
		if(fields[0]==NULL || fields[1]==NULL) continue;
		sol1 = sol2 = NULL;
		/* string concatnated by exon sequences of two genes */
		if((str1 =  concat_exons(fields[0], fasta_u, kmer_ht, _k, gname1, gname2, &ename1, &ename2, &junc_pos, opt->min_kmer_match))!=NULL){
			if((sol1 =align(fields[0], str1, junc_pos, opt->match, opt->mismatch, opt->gap, opt->extension, opt->jump_gene))!=NULL){
				if(sol1->jump == true && sol1->prob >= opt->min_align_score){
					/* idx = exon1.start.exon2.end (uniq id)*/
					idx = concat(concat(ename1, "."), ename2); // idx for junction
					HASH_FIND_STR(ret, idx, m);  
					if(m==NULL){ // add this junction to ret
						m = junction_init(opt->seed_len);				
						m->idx    = strdup(idx);
						m->exon1  = strdup(ename1);
						m->exon2  = strdup(ename2);				
						m->hits  = 1;
						m->likehood = 10*log(sol1->prob); 				
						// junction flanking sequence 
						memcpy( m->s, &str1[sol1->jump_start-opt->seed_len/2-1], opt->seed_len/2);
						memcpy( &m->s[opt->seed_len/2], &str1[sol1->jump_end], opt->seed_len/2);						
						// junction flanking sequence 
						strlen2 = sol1->jump_start + strlen(str1) - sol1->jump_end+1;				
						m->transcript = mycalloc(strlen2, char);
						m->junc_pos = sol1->jump_start;				
						memset(m->transcript, '\0', strlen2);
						memcpy( m->transcript, str1, sol1->jump_start);
						memcpy( &m->transcript[sol1->jump_start], &str1[sol1->jump_end+1], strlen(str1) - sol1->jump_end);				
						HASH_ADD_STR(ret, idx, m);
					}else{
						m->hits ++;
						m->likehood += 10*log(sol1->prob); 
					}
				}
			}
		}

		if((str2 =  concat_exons(fields[1], fasta_u, kmer_ht, _k, gname1, gname2, &ename1, &ename2, &junc_pos, opt->min_kmer_match))!=NULL){
			if((sol2 = align(fields[1], str2, junc_pos, opt->match, opt->mismatch, opt->gap, opt->extension, opt->jump_gene))!=NULL){
				if(sol2->jump == true && sol2->prob >= opt->min_align_score){			
					idx = concat(concat(ename1, "."), ename2); // idx for junction
					HASH_FIND_STR(ret, idx, m);
					if(m==NULL){ // this junction not in ret
						m = junction_init(opt->seed_len);				
						m->idx   = idx;
						m->exon1  = ename1;
						m->exon2  = ename2;				
						m->hits  = 1;
						m->likehood = 10*log(sol2->prob); 				
						memcpy( m->s, &str2[sol2->jump_start-opt->seed_len/2-1], opt->seed_len/2);
						memcpy( &m->s[opt->seed_len/2], &str2[sol2->jump_end], opt->seed_len/2);
						strlen2 = sol2->jump_start + strlen(str2) - sol2->jump_end+1;
						m->transcript = mycalloc(strlen2, char);
						m->junc_pos = sol2->jump_start;
						memset(m->transcript, '\0', strlen2);
						memcpy( m->transcript, str2, sol2->jump_start);
						memcpy( &m->transcript[sol2->jump_start], &str2[sol2->jump_end], strlen(str2)-sol2->jump_end);				
						HASH_ADD_STR(ret, idx, m);
					}else{
						m->hits ++;
						m->likehood += 10*log(sol2->prob); 
					}
				}	
			}
		}
	free(fields[0]); 	
	free(fields[1]); 	
	}
	// delete those junctions with hits < MIN_HITS
	HASH_ITER(hh, ret, m, n){
		if(m != NULL){
			m->likehood = m->likehood/m->hits;
			if(m->hits < opt->min_hits){
				HASH_DEL(ret,m);
				free(m);
			}
		}
	}
	if(ename1)         free(ename1);
	if(ename2)         free(ename2);
	if(str1)           free(str1);
	if(str2)           free(str2);
	if(sol1)           solution_destory(&sol1);
	if(sol2)           solution_destory(&sol2);
	if(idx)            free(idx);
	return ret;
}

static junction_t 
*transcript_construct_junc(junction_t *junc_ht, fasta_t *exon_ht){
	if(junc_ht==NULL || exon_ht==NULL) return NULL;
	junction_t *cur_junction, *tmp_junction;
	char* gname1, *gname2, *ename1, *ename2;
	gname1 = gname2 = ename1 = ename2 = NULL;
	int enum1, enum2;
	char enum1_str[10], enum2_str[10];
	int num1, num2;
	char **fields1, **fields2;
	fasta_t *exon1_fa,  *exon2_fa;
	exon1_fa = exon2_fa = NULL;
	char *exon1_seq, *exon2_seq;
	int i, j;
	int *S1, *S2;
	int str1_l, str2_l, str3_l;
	HASH_ITER(hh, junc_ht, cur_junction, tmp_junction) {
		exon1_seq = exon2_seq = NULL;
		S1 = S2 = NULL;
		cur_junction->S1_num = cur_junction->S2_num = 0;
		cur_junction->S1 = cur_junction->S2 = NULL;
		fields1 = strsplit(cur_junction->exon1, '.', &num1);
		fields2 = strsplit(cur_junction->exon2, '.', &num2);
		if(num1 != 2 || num2 != 2) continue;
		gname1 = fields1[0]; enum1 = atoi(fields1[1]);
		gname2 = fields2[0]; enum2 = atoi(fields2[1]);
		S1 = mycalloc(enum1+1, int);
		for(i=1; i<enum1; i++){
			sprintf(enum1_str, "%d", i);
			ename1 = concat(concat(gname1, "."), enum1_str);
			if((exon1_fa = find_fasta(exon_ht, ename1))!=NULL){
				exon1_seq = concat(exon1_seq, exon1_fa->seq);
				S1[i-1] = strlen(exon1_seq);
				cur_junction->S1_num = i;
			}
		}
		str1_l = (exon1_seq == NULL) ? 0 : strlen(exon1_seq);
		str2_l = (cur_junction->transcript == NULL) ? 0 : strlen(cur_junction->transcript);
		S2 = mycalloc(100, int); memset(S2, UINT_MAX, 100);
		S2[0] = str1_l + str2_l;
		j = 1;for(i=(enum2+1); i<INFINITY; i++){
			sprintf(enum2_str, "%d", i);
			ename2 = concat(concat(gname2, "."), enum2_str);
			if((exon2_fa = find_fasta(exon_ht, ename2))==NULL) break;
			exon2_seq = concat(exon2_seq, exon2_fa->seq);
			cur_junction->S2_num = j;
			S2[j++] = str1_l + str2_l + strlen(exon2_seq);
		}
		cur_junction->junc_pos = str1_l + cur_junction->junc_pos;
		cur_junction->transcript = concat(concat(exon1_seq, cur_junction->transcript), exon2_seq);
		cur_junction->S1 = S1;
		cur_junction->S2 = S2;
	}	
	if(fields1)       free(fields1);
	if(fields2)       free(fields2);
	if(gname1)        free(gname1);
	if(gname2)        free(gname2);
	return junc_ht;
}

static junction_t
*transcript_construct_no_junc(char* gname1, char *gname2, fasta_t *fasta_ht){
	if(gname1==NULL || gname2==NULL || fasta_ht==NULL) return NULL;	
	junction_t *junc_res = junction_init(10);
	fasta_t *fasta_cur;
	char *res1 = NULL;
	char *res2 = NULL;
	int num;
	int idx = 0;
	char** strs;
	int *S1, *S2;
	S1 = S2 = NULL;
	for(fasta_cur=fasta_ht; fasta_cur!=NULL; fasta_cur=fasta_cur->hh.next){
		S1 = mycalloc(100, int); memset(S1, 0, 100);		
		strs = strsplit(fasta_cur->name, '.', &num);
		if(num!=2) continue;
		if(strcmp(strs[0], gname1) != 0) continue;
		if(atoi(strs[1]) < idx) continue; // make sure the order
		res1 = concat(res1, fasta_cur->seq);
		S1[idx] = strlen(res1);
		idx = atoi(strs[1]);		
	}
	junc_res->S1_num = idx;
	
	idx = 0;
	for(fasta_cur=fasta_ht; fasta_cur!=NULL; fasta_cur=fasta_cur->hh.next){
		S2 = mycalloc(100, int); memset(S2, UINT_MAX, 100);
		strs = strsplit(fasta_cur->name, '.', &num);
		if(num!=2) continue;
		if(strcmp(strs[0], gname2) != 0) continue;
		if(atoi(strs[1]) < idx) continue; // make sure the order
		res2 = concat(res2, fasta_cur->seq);
		S2[idx] = strlen(res2);
		idx = atoi(strs[1]);
	}
	junc_res->S2_num = idx;
	junc_res->idx = concat(concat(gname1, "_"), gname2);
	junc_res->exon1 = NULL;
	junc_res->exon2 = NULL;
	junc_res->s = NULL;	
	junc_res->transcript = concat(res1, res2);	
	junc_res->junc_pos = strlen(res1);
	junc_res->S1 = S1;
	junc_res->S2 = S2;	
	junc_res->hits = 0;
	junc_res->likehood = -INFINITY;
	return junc_res;
}

/*
 * generate junction string of every edge based on supportive reads.
 */
static int
bag_junction_gen(bag_t **bag, fasta_t *fa, kmer_t *kmer, opt_t *opt){
	if(*bag==NULL || fa==NULL || opt==NULL) return -1;	
	bag_t *edge, *bag_cur;
	register int i;
	junction_t *junc_cur;
	for(edge=*bag; edge!=NULL; edge=edge->hh.next) {
		if((junc_cur = edge_junction_gen(edge, fa, kmer, opt))==NULL){ // no junction detected
			edge->junc_flag = false;
			edge->junc = NULL;
		}else{
			edge->junc_flag = true;
			edge->junc = junc_cur;			
		}		
	}   
	return 0;
}


/*
 * generate junction string of every edge based on supportive reads.
 */
static int
bag_transcript_gen(bag_t **bag, fasta_t *fa, opt_t *opt){
	if(*bag==NULL || fa==NULL || opt==NULL) return -1;	
	bag_t *edge, *bag_cur;
	register int i;
	junction_t *junc_cur;
	for(edge=*bag; edge!=NULL; edge=edge->hh.next) {
		if(edge->junc_flag==true){ // if junction identified before
			edge->junc = transcript_construct_junc(edge->junc, fa);
		}else{
			edge->junc = transcript_construct_no_junc(edge->gname1, edge->gname2, fa);
		}
	}   
	return 0;
}
/*
 * construct concatnated exon string based on kmer matches
 */
static char 
*concat_exons(char* _read, fasta_t *fa_ht, kmer_t *kmer_ht, int _k, char *gname1, char* gname2, char** ename1, char** ename2, int *junc_pos, int min_kmer_match){
	if(_read == NULL || fa_ht == NULL || kmer_ht==NULL || gname1==NULL || gname2==NULL) return NULL;
	/* variables */
	char *str1, *str2, *gname_cur;
	*ename1 = *ename2 = str1 = str2 = gname_cur = NULL;
	int num_tmp;
	char buff[_k];
	str_ctr *s_ctr, *exons=NULL;
	fasta_t *fa_tmp = NULL;
	/* find all exons that uniquely match with gene by kmer */
	find_all_exons(&exons, kmer_ht, _read, _k);
	if(exons==NULL) return NULL; // no exon found
	for(s_ctr=exons; s_ctr!=NULL; s_ctr=s_ctr->hh.next){
		if(s_ctr->SIZE >= min_kmer_match){ //denoise
			gname_cur = strsplit(s_ctr->KEY, '.', &num_tmp)[0]; // name of gene
			// extract string of gene1
			if(strcmp(gname_cur, gname1)==0){
				fa_tmp = find_fasta(fa_ht, s_ctr->KEY);
				if(str1 == NULL){
					str1 = strdup(fa_tmp->seq);
					*ename1 = strdup(s_ctr->KEY);
				}else{
					*ename1 = strdup(s_ctr->KEY);
					str1 = concat(str1, fa_tmp->seq);
				}				
			}
			// extract string of gene2
			if(strcmp(gname_cur, gname2)==0){
				fa_tmp = find_fasta(fa_ht, s_ctr->KEY);
				if(str2 == NULL){
					str2 = strdup(fa_tmp->seq);
					*ename2 = strdup(s_ctr->KEY);
				}else{
					str2 = concat(str2, fa_tmp->seq);
				}				
			}
		}
	}
	if(gname_cur)     free(gname_cur);
	if(exons)         str_ctr_destory(&exons);
	if(str1 == NULL || str2 == NULL) return NULL; // only if two genes identified
	char *ret = concat(str1, str2);
	*junc_pos = strlen(str1);	
	return ret;
}

static int test_fusion(solution_pair_t **res, bag_t **bag, opt_t *opt){
	if(*bag==NULL || opt==NULL) return -1;
	bag_t *edge;
	for(edge=*bag; edge!=NULL; edge=edge->hh.next){		
		if((update_fusion(&edge, res, opt))!=0) return -1;
	}
	return 0;
}

/*
 * align supportive reads of every edge to edge's constructed transcript
 */
static int 
update_fusion(bag_t **edge, solution_pair_t **res, opt_t *opt){
	if(*edge==NULL || opt==NULL) return -1;
	char* junc_name = NULL;
	int weight = (*edge)->weight;
	(*edge)->weight = (*edge)->likehood = 0;
	int num, rc;
	register int i, j;
	solution_t *sol1, *sol2; sol1 = sol2 = NULL;
	solution_pair_t *sol_cur;
	junction_t *junc_cur;
	register char *read1, *read2; read1 = read2 = NULL;
	char** fields = NULL;
	/* iterate every supportive read pair */
	for(i=0; i<weight; i++){
		if((*edge)->evidence[i]==NULL || (*edge)->read_names[i]==NULL) continue;
		if((sol_cur = find_solution_pair(*res, (*edge)->read_names[i]))!=NULL){
			if(strcmp(sol_cur->fuse_name, (*edge)->edge)==0) continue;					
		}
		fields = NULL;
		fields = strsplit((*edge)->evidence[i], '_', &num);
		if(num!=2){for(j=0; j<num; j++) free(fields[j]); continue;}
		read1 = fields[0]; 		read2 = fields[1]; 
		/* iterate every junction then */
		for(junc_cur=(*edge)->junc; junc_cur!=NULL; junc_cur=junc_cur->hh.next){
			/* release every memory used */
			if((sol1 = align_exon_jump(read1, junc_cur->transcript, junc_cur->S1, junc_cur->S2, junc_cur->S1_num, junc_cur->S2_num, opt->match, opt->mismatch, opt->gap, opt->extension, opt->jump_exon))==NULL) continue;
			if(sol1->prob < opt->min_align_score){solution_destory(&sol1); continue;}
			if((sol2 = align_exon_jump(read2, junc_cur->transcript, junc_cur->S1, junc_cur->S2, junc_cur->S1_num, junc_cur->S2_num, opt->match, opt->mismatch, opt->gap, opt->extension, opt->jump_exon))==NULL) continue;			
			if(sol2->prob < opt->min_align_score){solution_destory(&sol2); continue;}
			
			if(sol_cur!=NULL){ // if exists and update if align score is high enough
				if(sol_cur->prob < sol1->prob*sol2->prob){
					sol_cur->r1        = sol1; 
					sol_cur->r2        = sol2; 
					sol_cur->prob      = (sol1->prob)*(sol2->prob); 
					sol_cur->junc_name = ((*edge)->junc_flag==true) ? junc_cur->idx : NULL;					
					sol_cur->fuse_name = (*edge)->edge;
				}else{ // sol1 and sol2 not used
					solution_destory(&sol1);
					solution_destory(&sol2);
				}
			}else{
					sol_cur = solution_pair_init();
					sol_cur->idx = strdup((*edge)->read_names[i]); 
					sol_cur->r1 = sol1;			
					sol_cur->r2 = sol2;
					sol_cur->prob = sol1->prob*sol2->prob;
					sol_cur->junc_name = ((*edge)->junc_flag==true) ? junc_cur->idx : NULL;					
					sol_cur->fuse_name = (*edge)->edge;
					HASH_ADD_STR(*res, idx, sol_cur);				
			}
		}
		if(fields[0])    free(fields[0]);
		if(fields[1])    free(fields[1]);
	}	
	return 0;
}

/*
 * Description:
 *------------
 * 1) find subset of pairs that contain 20bp junction string by at most 2 mismatches
 * 2) align those reads to constructed transcript returned by transcript_construct

 * Input: 
 *-------
 * junc_ht        - junction_t object: return by transcript_construct
 * opt            - opt_t object: contains all input parameters

 * Output: 
 *-------
 * solution_pair_t object that contains alignment results of all reads.
 */
static int test_junction(solution_pair_t **res, bag_t **bag, opt_t *opt){
	if(*bag==NULL || opt==NULL) return -1;
	bag_t *bag_cur;
	junction_t *junc_cur;
	char* junc_name;
	for(bag_cur=*bag; bag_cur!=NULL; bag_cur=bag_cur->hh.next){		
		if(bag_cur->junc_flag==false) continue;
		fprintf(stderr, "[predict] junctions between %s and %s is being tested ... \n", bag_cur->gname1, bag_cur->gname2);		
		for(junc_cur=bag_cur->junc; junc_cur!=NULL; junc_cur=junc_cur->hh.next){
			if(junc_cur->s==NULL || junc_cur->transcript==NULL || junc_cur->S1==NULL ||  junc_cur->S2==NULL) continue;
			junc_name = (bag_cur->junc_flag==true) ? junc_cur->idx : NULL;
			if((update_junction(&junc_cur, res, opt, bag_cur->edge, junc_name))!=0) return -1;
		}
	}
	return 0;
}


/*
 * align reads to one junction transcript

 * junc      - one junction identified before
 * opt       - opt_t object
 * *sol_pair - solution_pair_t object that contains alignment solutions for all read pair agains junc

 */
static int update_junction(junction_t **junc, solution_pair_t **sol_pair, opt_t *opt, char* fuse_name, char* junc_name){
	if(*junc==NULL || opt==NULL || fuse_name==NULL) return -1;
	// junction
	(*junc)->hits     = 0;
	(*junc)->likehood = 0;

	gzFile fp1, fp2;
	int l1, l2;
	kseq_t *seq1, *seq2;
	register char *_read1, *_read2;
	solution_t *sol1, *sol2;
	sol1 = sol2 = NULL;
	solution_pair_t *s_sp, *tmp_sp;
	if((fp1  = gzopen(opt->fq1, "r")) == NULL)   die("[%s] fail to read fastq files\n",  __func__);
	if((fp2  = gzopen(opt->fq2, "r")) == NULL)   die("[%s] fail to read fastq files\n",  __func__);	
	if((seq1 = kseq_init(fp1))   == NULL)        die("[%s] fail to read fastq files\n",  __func__);
	if((seq2 = kseq_init(fp2))   == NULL)        die("[%s] fail to read fastq files\n",  __func__);	
	
	while ((l1 = kseq_read(seq1)) >= 0 && (l2 = kseq_read(seq2)) >= 0 ) {
		_read1 = rev_com(seq1->seq.s); // reverse complement of read1
		_read2 = strdup(seq2->seq.s);		
		if(_read1 == NULL || _read2 == NULL){
			if(_read1)  free(_read1);
			if(_read2)  free(_read2);
			continue;	
		}
		if((min_mismatch(_read1, (*junc)->s)) <= opt->max_mismatch || (min_mismatch(_read2, (*junc)->s)) <= opt->max_mismatch ){	
			// alignment with jump state between exons 
			if((sol1 = align_exon_jump(_read1, (*junc)->transcript, (*junc)->S1, (*junc)->S2, (*junc)->S1_num, (*junc)->S2_num, opt->match, opt->mismatch, opt->gap, opt->extension, opt->jump_exon))==NULL) continue;
			if(sol1->prob < opt->min_align_score){ solution_destory(&sol1); continue;}
			if((sol2 = align_exon_jump(_read2, (*junc)->transcript, (*junc)->S1, (*junc)->S2, (*junc)->S1_num, (*junc)->S2_num, opt->match, opt->mismatch, opt->gap, opt->extension, opt->jump_exon))==NULL) continue;
			if(sol2->prob < opt->min_align_score){solution_destory(&sol2);  continue;}
			s_sp = find_solution_pair(*sol_pair, seq1->name.s);
			if(s_sp!=NULL){ // if exists
				if(s_sp->prob < sol1->prob*sol2->prob){
					(*junc)->hits ++;
					(*junc)->likehood += 10*log(sol1->prob); 				
					(*junc)->likehood += 10*log(sol2->prob);
					s_sp->r1 = sol1; s_sp->r2=sol2; 
					s_sp->prob = (sol1->prob)*(sol2->prob); 
					s_sp->junc_name = strdup(junc_name);
					s_sp->fuse_name = strdup(fuse_name);		
				}else{
					if(sol1) solution_destory(&sol1);
					if(sol2) solution_destory(&sol2);	
				}
			}else{
				if(sol1->prob >= opt->min_align_score && sol2->prob >= opt->min_align_score){
					(*junc)->hits ++;
					(*junc)->likehood += 10*log(sol1->prob); 				
					(*junc)->likehood += 10*log(sol2->prob); 				
					s_sp = solution_pair_init();
					s_sp->idx = strdup(seq1->name.s); 
					s_sp->junc_name = strdup(junc_name);
					s_sp->fuse_name = strdup(fuse_name);				
					s_sp->r1 = sol1;			
					s_sp->r2 = sol2;
					s_sp->prob = sol1->prob*sol2->prob;
					HASH_ADD_STR(*sol_pair, idx, s_sp);
				}
			}
		}
		if(_read1)  free(_read1);
		if(_read2)  free(_read2);
	}
	if(seq1)     kseq_destroy(seq1);
	if(seq2)     kseq_destroy(seq2);	
	if(fp1)      gzclose(fp1);
	if(fp2)      gzclose(fp2);
	return 0;	
}


/*
 * Description:
 *------------
 * revisit junction sites and score them based on alignment results
 
 * Input: 
 *-------
 * sol        - alignment results returned by align_to_transcript
 * junc       - previously identified junction
 * opt        - opt_t object: contains all input parameters

 * Output: 
 *-------
 * junction_t object that contains identified junctions with one more property -> transcript.
 */
static junction_t *junction_score(solution_pair_t *sol, junction_t *junc, double min_align_score, int junc_str_len){
	if(sol==NULL || junc==NULL) return NULL;
	junction_t *junc_cur1, *junc_cur2, *junc_res = NULL;
	solution_pair_t *sol_cur, *sol_tmp;
	double th = min_align_score * min_align_score;
	// iterate every alignment solution pair
	HASH_ITER(hh, sol, sol_cur, sol_tmp) {
		if(sol_cur->prob < th) continue; // filter read pairs with alignment identity smaller than th
		if((junc_cur1 = find_junction(junc, sol_cur->junc_name))==NULL) continue; // continue if junction not exists
		if((junc_cur2 = find_junction(junc_res, sol_cur->junc_name))==NULL){
			junc_cur2 = junction_init(junc_str_len);
			junc_cur2->idx = junc_cur1->idx;
			junc_cur2->exon1 = junc_cur1->exon1;
			junc_cur2->exon2 = junc_cur1->exon2;			
			junc_cur2->s = junc_cur1->s;			
			junc_cur2->junc_pos = junc_cur1->junc_pos;			
			junc_cur2->transcript = junc_cur1->transcript;			
			junc_cur2->hits = 1;	
			junc_cur2->likehood = 10*log(sol_cur->prob);
			HASH_ADD_STR(junc_res, idx, junc_cur2);
		}else{
			junc_cur2->hits++;	
			junc_cur2->likehood += 10*log(sol_cur->prob);			
		}
	}
	return junc_res;
}


static gene_t *fasta_get_info(fasta_t *fa_ht){
	if(fa_ht==NULL) return NULL;
	fasta_t *fa_cur;
	gene_t *gene_cur, *gene_ret=NULL;
	char** fields;
	int num;
	int i, j;
	for(fa_cur=fa_ht; fa_cur!=NULL; fa_cur=fa_cur->hh.next){
		fields=NULL;
		fields = strsplit(fa_cur->name, '.', &num);
		if(num!=2){for(i=0; i<num; i++) free(fields[i]); continue;}
		if((gene_cur = find_gene(gene_ret, fields[0]))==NULL){
			gene_cur = gene_init();
			gene_cur->name = strdup(fields[0]);
			gene_cur->exon_num = 1;
			gene_cur->hits = 0;
			gene_cur->len = strlen(fa_cur->seq);
			HASH_ADD_STR(gene_ret, name, gene_cur);
		}else{
			gene_cur->exon_num ++;
			gene_cur->len += strlen(fa_cur->seq);			
		}
		if(fields[0])   free(fields[0]);
		if(fields[1])   free(fields[1]);
	}
	return gene_ret;
}

static int prob_sort(solution_pair_t *a, solution_pair_t *b){
	return ((a->prob - b->prob) > 0) ? 1 : -1;	
}

static solution_pair_t *solution_uniq(solution_pair_t *sol){
	if(sol==NULL) return NULL;
	HASH_SORT(sol, prob_sort);
	solution_pair_t *sol_cur, *sol_i, *sol_j, *sol_ret = NULL;
	int i, j;
	i = 0;
	for(sol_i=sol; sol_i!=NULL; sol_i=sol_i->hh.next){
		sol_cur = solution_pair_init();
		for(sol_j=sol_i; sol_j!=NULL; sol_j=sol_j->hh.next){
			if((strcmp(sol_i->fuse_name, sol_j->fuse_name)==0) && (strcmp(sol_i->junc_name, sol_j->junc_name)==0) && (sol_i->r1->pos==sol_j->r1->pos) && (sol_i->r2->pos==sol_j->r2->pos)){
				sol_cur = solution_pair_copy(sol_j);
			}
		}		
		if(sol_cur!=NULL){if((find_solution_pair(sol_ret, sol_cur->idx))==NULL){
				HASH_ADD_STR(sol_ret, idx, sol_cur);				
		}}
	}
	return sol_ret;
}

static int fuse_score(solution_pair_t *sol, bag_t **bag, gene_t *gene, back_t *back, opt_t *opt){
	if(sol==NULL || *bag==NULL) return -1;
	solution_pair_t *sol_cur;
	bag_t *bag_cur, *bag_tmp;
	back_t *back_cur = NULL;
	int alpha = opt->alpha;
	float prob;
	int i, j;
	/* initilize */
	for(bag_cur=*bag; bag_cur!=NULL; bag_cur=bag_cur->hh.next){
		bag_cur->likehood = 0;
		bag_cur->weight = 0;
	}
	for(sol_cur=sol; sol_cur!=NULL; sol_cur=sol_cur->hh.next){
		if((bag_cur=find_edge(*bag, sol_cur->fuse_name))!=NULL){
			prob = sol_cur->r1->prob*sol_cur->r2->prob;
			bag_cur->likehood += (sol_cur->junc_name!=NULL) ? -alpha*log10(1.1 - prob) : -log10(1.1 - prob);
			bag_cur->weight++;
		}
	}
	
	gene_t *cur_gene1, *cur_gene2;
	float num = 1;	
	
	HASH_ITER(hh, *bag, bag_cur, bag_tmp){
		num = 1;
		if(bag_cur->weight < opt->min_edge_weight){
			HASH_DEL(*bag,bag_cur);
			free(bag_cur);
			continue;
		}
		bag_cur->pvalue = 1;
		cur_gene1 = find_gene(gene, bag_cur->gname1);
		cur_gene2 = find_gene(gene, bag_cur->gname2);
		if(cur_gene1==NULL || cur_gene2==NULL){
			if(cur_gene1) gene_destory(&cur_gene1);
			if(cur_gene2) gene_destory(&cur_gene2);
			continue;
		}
		bag_cur->likehood = (bag_cur->likehood/(cur_gene1->hits + cur_gene2->hits+1))*1000000;
		if((back_cur = find_back(back, bag_cur->edge))!=NULL){
			for(i=0; i<back_cur->arr_num; i++){
				if(back_cur->arr[i]>=bag_cur->likehood) num++; 
			}
		}
		bag_cur->pvalue = num/200;
	}
	return 0;
}

static int output(bag_t *bag, gene_t *gene, opt_t *opt){
	if(bag==NULL) return -1;
	if(HASH_COUNT(bag)==0) return -1;
	bag_t  *cur_bag;
	for(cur_bag=BAGR_HT; cur_bag!=NULL; cur_bag=cur_bag->hh.next)
		printf("%s\t%s\t%5d\tscore=%.2f\tpvalue=%f\n", cur_bag->gname1, cur_bag->gname2, cur_bag->weight, cur_bag->likehood, cur_bag->pvalue);
	return 0;
}

static int pred_usage(opt_t *opt){
	fprintf(stderr, "\n");
			fprintf(stderr, "Usage:   tafuco predict [options] <gname.txt> <genes.gtf> <in.fa> <R1.fq> <R2.fq>\n\n");
			fprintf(stderr, "Details: predict gene fusion from pair-end RNA-seq data\n\n");
			fprintf(stderr, "Options:\n");
			
			fprintf(stderr, "   -- Graph:\n");
			fprintf(stderr, "         -k INT    kmer length for indexing in.fa [%d]\n", opt->k);
			fprintf(stderr, "         -n INT    min unique kmer matches for a hit between gene and pair [%d]\n", opt->min_kmer_match);
			fprintf(stderr, "         -w INT    edges in graph of weight smaller than -w will be removed [%d]\n", opt->min_edge_weight);
			
			fprintf(stderr, "   -- Alignment:\n");
			fprintf(stderr, "         -m INT    score for match [%d]\n", opt->match);
			fprintf(stderr, "         -u INT    penality for mismatch[%d]\n", opt->mismatch);
			fprintf(stderr, "         -o INT    penality for gap open [%d]\n", opt->gap);
			fprintf(stderr, "         -e INT    penality for gap extension [%d]\n", opt->extension);
			fprintf(stderr, "         -j INT    penality for jump between genes [%d]\n", opt->jump_gene);
			fprintf(stderr, "         -s INT    penality for jump between exons [%d]\n", opt->jump_exon);
			fprintf(stderr, "         -a FLOAT  min identity score for alignment [%.2f]\n", opt->min_align_score);
			
			fprintf(stderr, "   -- Junction:\n");
			fprintf(stderr, "         -h INT    min hits for a junction [%d]\n", opt->min_hits);					
			fprintf(stderr, "         -l INT    length for junction string [%d]\n", opt->seed_len);
			fprintf(stderr, "         -x INT    max mismatches allowed for junction string match [%d]\n", opt->max_mismatch);					

			fprintf(stderr, "   -- Fusion:\n");
			fprintf(stderr, "         -A INT    weight for junction containing reads [%d]\n", opt->alpha);					
			fprintf(stderr, "         -p FLOAT  p-value cutoff for fusions [%.2f]\n", opt->pvalue);
			
			fprintf(stderr, "\n");
			fprintf(stderr, "Inputs:  gname.txt plain txt file that contains name of gene candidates\n");
			fprintf(stderr, "         genes.gtf gtf file that contains gene annotation\n");
			fprintf(stderr, "         in.fa     fasta file that contains reference genome\n");
			fprintf(stderr, "         R1.fq     5'->3' end of pair-end sequencing reads\n");
			fprintf(stderr, "         R2.fq     the other end of sequencing reads\n");
			return 1;
}

static fasta_t* convert_exon_seq(fasta_t *fa){
	if(fa==NULL) return NULL;
	fasta_t *ret = NULL;
	fasta_t *s, *s_fasta;
	char* name;
	char idx[15];
	str_ctr *s_ctr, *ctr = NULL;
	
	for(s=fa; s!=NULL; s=s->hh.next){
		str_ctr_add(&ctr, s->gene_name);
		s_ctr = find_ctr(ctr, s->gene_name);
		sprintf(idx, "%d", s_ctr->SIZE);
		name = join(3, s->gene_name, ".", idx);				
		s_fasta = fasta_init();
		s_fasta->name = strdup(name);	
		s_fasta->seq = strdup(s->seq);	
		HASH_ADD_STR(ret, name, s_fasta);		
		free(name);
	}
	return ret;
}

static back_t* read_background(char* fname){
	if(fname==NULL) return NULL;
	back_t *back_cur, *back_ret = NULL;
	char  *line = NULL;
	size_t len = 0;
	int l;
	ssize_t read;
	char  **fields = NULL;
	int num; 
	char *edge_name;
	int i, j;
	FILE * fp = fopen (fname, "r");
	if(fp==NULL) die("[%s] can't open %s", __func__, fname); 
	while ((read = getline(&line, &len, fp)) != -1) {
		if((fields = strsplit(line, 0, &num))==NULL) continue; // get rid of \n 
		if(num==4){
			int rc = strcmp(fields[0], fields[1]);
			if(rc<0)  edge_name = concat(concat(fields[0], "_"), fields[1]);
			if(rc>0)  edge_name = concat(concat(fields[1], "_"), fields[0]);
			if(rc==0) edge_name = NULL;
			if(edge_name!=NULL){
				if((back_cur = find_back(back_ret, edge_name))==NULL){
					back_cur = back_init();
					back_cur->key = strdup(edge_name);
					back_cur->arr_num = 1;
					back_cur->arr = mycalloc(1, float);
					back_cur->arr[0] = atof(fields[3]);
					HASH_ADD_STR(back_ret, key, back_cur);
				}else{
					back_cur->arr_num ++;
					back_cur->arr = realloc(back_cur->arr, back_cur->arr_num*sizeof(*back_cur->arr));
					back_cur->arr[back_cur->arr_num-1] = atof(fields[3]);
				}
			}		
		}
		if(fields){
			for(i=0; i<num; i++) free(fields[i]);
			free(fields);
		}
		
		free(edge_name);
	}
	fclose(fp);
	return back_ret;
}
/*--------------------------------------------------------------------*/
/*  predict  */
int predict(int argc, char *argv[]) {
	opt_t *opt = opt_init(); // initlize options with default settings
	int c, i;
	srand48(11);
	while ((c = getopt(argc, argv, "m:w:k:n:u:o:e:g:s:h:l:x:a:")) >= 0) {
				switch (c) {
				case 'k': opt->k = atoi(optarg); break;	
				case 'n': opt->min_kmer_match = atoi(optarg); break;
				case 'w': opt->min_edge_weight = atoi(optarg); break;
				case 'm': opt->match = atoi(optarg); break;
				case 'u': opt->mismatch = atoi(optarg); break;
				case 'o': opt->gap = atoi(optarg); break;
				case 'e': opt->extension = atoi(optarg); break;
				case 'j': opt->jump_gene = atoi(optarg); break;
				case 's': opt->jump_exon = atoi(optarg); break;
				case 'h': opt->min_hits = atoi(optarg); break;
				case 'l': opt->seed_len = atoi(optarg); break;
				case 'x': opt->max_mismatch = atoi(optarg); break;
				case 'a': opt->min_align_score = atof(optarg); break;
				default: return 1;
		}
	}

	if (optind + 5 > argc) return pred_usage(opt);
	opt->gfile  = argv[optind];    // gnames.txt
	opt->gtf  = argv[optind+1];    // genes.gtf
	opt->fa  = argv[optind+2];     // hg19.fa
	opt->fq1 = argv[optind+3];     // read1.fq
	opt->fq2 = argv[optind+4];     // read2.fq
	
	if(opt->k < MIN_KMER_LEN || opt->k > MAX_KMER_LEN) die("[%s] -k must be within [%d, %d]", __func__, MIN_KMER_LEN, MAX_KMER_LEN); 	
	if(opt->min_kmer_match < MIN_MIN_KMER_MATCH) die("[%s] -n must be within [%d, +INF)", __func__,   MIN_MIN_KMER_MATCH); 	
	if(opt->min_edge_weight < MIN_MIN_EDGE_WEIGHT) die("[%s] -w must be within [%d, +INF)", __func__, MIN_MIN_EDGE_WEIGHT); 	
	if(opt->min_hits < MIN_MIN_HITS) die("[%s] -h must be within [%d, +INF)", __func__, MIN_MIN_HITS); 	
	if(opt->min_align_score < MIN_MIN_ALIGN_SCORE || opt->min_align_score > MAX_MIN_ALIGN_SCORE) die("[%s] -a must be within [%d, %d]", __func__, MIN_MIN_ALIGN_SCORE, MAX_MIN_ALIGN_SCORE); 	
	
	fprintf(stderr, "[%s] loading reference genome sequences ... \n",__func__);
	if((GENO_HT = fasta_read(opt->fa)) == NULL) die("[%s] can't load reference genome %s", __func__, opt->fa);	
	fasta_t *exon_tmp = NULL;
	fprintf(stderr, "[%s] Exracting exon sequences ... \n",__func__);
	if((exon_tmp = extract_exon_seq(opt->gfile, opt->gtf, GENO_HT, "exon"))==NULL) die("[%s] can't extract exon sequences of %s", __func__, opt->gfile);
	if(GENO_HT)          fasta_destroy(&GENO_HT);

	EXON_HT = convert_exon_seq(exon_tmp);
	if(exon_tmp)          fasta_destroy(&exon_tmp);
	
	fprintf(stderr, "[%s] getting genes infomration ... \n",__func__);
	if((GENE_HT = fasta_get_info(EXON_HT)) == NULL) die("[%s] fail to gene genes' information", __func__);	
	
	fprintf(stderr, "[%s] indexing sequneces by kmer hash table ... \n",__func__);
	if((KMER_HT = kmer_index(EXON_HT, opt->k))==NULL) die("[%s] can't index exon sequences", __func__);
    
	fprintf(stderr, "[%s] constructing breakend associated graph ... \n", __func__);
	if((BAGR_HT = bag_construct(KMER_HT, &GENE_HT, opt->fq1, opt->fq2, opt->min_kmer_match, opt->min_edge_weight, opt->k)) == NULL) return 0;
	//
	fprintf(stderr, "[%s] triming graph by removing edges of weight smaller than %d... \n", __func__, opt->min_edge_weight);
	if(bag_trim(&BAGR_HT, opt->min_edge_weight)!=0){
		fprintf(stderr, "[%s] fail to trim graph \n", __func__);
		return -1;
	}
	if(BAGR_HT == NULL) return 0;
	
	fprintf(stderr, "[%s] identifying junctions for every fusion candiates... \n", __func__);
	if(bag_junction_gen(&BAGR_HT, EXON_HT, KMER_HT, opt)!=0){
		fprintf(stderr, "[%s] fail to identify junctions\n", __func__);
		return -1;	
	}
	if(BAGR_HT == NULL) return 0;
	
    fprintf(stderr, "[%s] constructing transcript for identified junctions ... \n", __func__);		
    if((bag_transcript_gen(&BAGR_HT, EXON_HT, opt))!=0){
    	fprintf(stderr, "[%s] fail to construct transcript\n", __func__);
    	return -1;	
    }
    
    fprintf(stderr, "[%s] testing junctions ... \n", __func__);		
    if((test_junction(&SOLU_HT, &BAGR_HT, opt))!=0){
    	fprintf(stderr, "[%s] fail to rescan reads\n", __func__);
    	return -1;		
    }
	 
    fprintf(stderr, "[%s] testing fusion ... \n", __func__);			
    if((test_fusion(&SOLU_HT, &BAGR_HT, opt))!=0){
    	fprintf(stderr, "[%s] fail to align supportive reads to transcript\n", __func__);
    	return -1;			
    }
	
	/* get rid of the duplicate reads*/
	//if((SOLU_UNIQ_HT = solution_uniq(SOLU_HT))==NULL) return 0;
	
	if(SOLU_HT==NULL){
    	fprintf(stderr, "[%s] no fusion identified\n", __func__);
    	return 0;		
	}
    
	/* score the fusion */
	if(fuse_score(SOLU_HT, &BAGR_HT, GENE_HT, BACK_HT, opt)!=0){
    	fprintf(stderr, "[%s] fail to score fusion\n", __func__);
    	return -1;		
	}
	
	output(BAGR_HT, GENE_HT, opt);
		
	fprintf(stderr, "[%s] cleaning up ... \n", __func__);
	if(EXON_HT)          fasta_destroy(&EXON_HT);
	if(KMER_HT)           kmer_destroy(&KMER_HT);
	if(BAGR_HT)            bag_destory(&BAGR_HT);
	if(SOLU_HT)  solution_pair_destory(&SOLU_HT);
	if(SOLU_UNIQ_HT)  solution_pair_destory(&SOLU_UNIQ_HT);
	if(GENE_HT)           gene_destory(&GENE_HT);
	fprintf(stderr, "[%s] congradualtions! it succeeded! \n", __func__);	
	return 0;
}

static int rapid_usage(opt_t *opt){
	fprintf(stderr, "\n");
			fprintf(stderr, "Usage:   tafuco rapid <R1.fq> <R2.fq>\n\n");
			fprintf(stderr, "Details: predict fusions in a rapid mode\n\n");
			fprintf(stderr, "Inputs:  R1.fq     5'->3' end of pair-end sequencing reads\n");
			fprintf(stderr, "         R2.fq     the other end of sequencing reads\n");
			return 1;
}

/*--------------------------------------------------------------------*/
/* rapid mode of prediction. */
int rapid(int argc, char *argv[]) {
	opt_t *opt = opt_init(); // initlize options with default settings
	int c, i;
	srand48(11);

	if (optind + 2 > argc) return rapid_usage(opt);
	opt->fq1 = argv[optind+0];  // read1
	opt->fq2 = argv[optind+1];  // read2
	BACK_HT = read_background(BACKGROUND_FILE);

	opt->fa = FASTA_NAME;
	fprintf(stderr, "[%s] loading sequences of targeted genes ... \n",__func__);
	if((EXON_HT = fasta_read(opt->fa)) == NULL) die("[%s] fail to read %s", __func__, opt->fa);	
    
	fprintf(stderr, "[%s] getting genes infomration ... \n",__func__);
	if((GENE_HT = fasta_get_info(EXON_HT)) == NULL) die("[%s] fail to gene genes' information", __func__);	
	
	fprintf(stderr, "[%s] indexing sequneces by kmer hash table ... \n",__func__);
	if((KMER_HT = kmer_index(EXON_HT, opt->k))==NULL) die("[%s] can't index exon sequences", __func__);
    
	fprintf(stderr, "[%s] constructing breakend associated graph ... \n", __func__);
	if((BAGR_HT = bag_construct(KMER_HT, &GENE_HT, opt->fq1, opt->fq2, opt->min_kmer_match, opt->min_edge_weight, opt->k)) == NULL) return 0;
	
	fprintf(stderr, "[%s] triming graph by removing edges of weight smaller than %d... \n", __func__, opt->min_edge_weight);
	if(bag_trim(&BAGR_HT, opt->min_edge_weight)!=0){
		fprintf(stderr, "[%s] fail to trim graph \n", __func__);
		return -1;
	}
	if(BAGR_HT == NULL) return 0;
	
	fprintf(stderr, "[%s] identifying junctions for every fusion candiates... \n", __func__);
	if(bag_junction_gen(&BAGR_HT, EXON_HT, KMER_HT, opt)!=0){
		fprintf(stderr, "[%s] fail to identify junctions\n", __func__);
		return -1;	
	}
	if(BAGR_HT == NULL) return 0;
	
    fprintf(stderr, "[%s] constructing transcript for identified junctions ... \n", __func__);		
    if((bag_transcript_gen(&BAGR_HT, EXON_HT, opt))!=0){
    	fprintf(stderr, "[%s] fail to construct transcript\n", __func__);
    	return -1;	
    }
    
    fprintf(stderr, "[%s] testing junctions ... \n", __func__);		
    if((test_junction(&SOLU_HT, &BAGR_HT, opt))!=0){
    	fprintf(stderr, "[%s] fail to rescan reads\n", __func__);
    	return -1;		
    }
	 
    fprintf(stderr, "[%s] testing fusion ... \n", __func__);			
    if((test_fusion(&SOLU_HT, &BAGR_HT, opt))!=0){
    	fprintf(stderr, "[%s] fail to align supportive reads to transcript\n", __func__);
    	return -1;			
    }
	
	/* get rid of the duplicate reads*/
	//if((SOLU_UNIQ_HT = solution_uniq(SOLU_HT))==NULL) return 0;
	
	if(SOLU_HT==NULL){
    	fprintf(stderr, "[%s] no fusion identified\n", __func__);
    	return 0;		
	}

	/* score the fusion */
	if(fuse_score(SOLU_HT, &BAGR_HT, GENE_HT, BACK_HT, opt)!=0){
    	fprintf(stderr, "[%s] fail to score fusion\n", __func__);
		return -1;		
	}
	
	output(BAGR_HT, GENE_HT, opt);
	
		
	fprintf(stderr, "[%s] cleaning up ... \n", __func__);
	if(EXON_HT)          fasta_destroy(&EXON_HT);
	if(KMER_HT)           kmer_destroy(&KMER_HT);
	if(BAGR_HT)            bag_destory(&BAGR_HT);
	if(SOLU_HT)  solution_pair_destory(&SOLU_HT);
	if(SOLU_UNIQ_HT)  solution_pair_destory(&SOLU_UNIQ_HT);
	if(GENE_HT)           gene_destory(&GENE_HT);
	if(BACK_HT)           back_destory(&BACK_HT);
	fprintf(stderr, "[%s] congradualtions! it succeeded! \n", __func__);	
	return 0;
}

