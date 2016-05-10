#include "proxysql.h"
#include "cpp.h"

#include "SpookyV2.h"

extern MySQL_STMT_Manager *GloMyStmt;




static uint64_t stmt_compute_hash(unsigned int hostgroup, char *user, char *schema, char *query, unsigned int query_length) {
	int l=0;
	l+=sizeof(hostgroup);
	l+=strlen(user);
	l+=strlen(schema);
// two random seperators
#define _COMPUTE_HASH_DEL1_ "-ujhtgf76y576574fhYTRDFwdt-"
#define _COMPUTE_HASH_DEL2_ "-8k7jrhtrgJHRgrefgreRFewg6-"
	l+=strlen(_COMPUTE_HASH_DEL1_);
	l+=strlen(_COMPUTE_HASH_DEL2_);
	l+=query_length;
	char *buf=(char *)malloc(l);
	l=0;
	// write hostgroup
	memcpy(buf,&hostgroup,sizeof(hostgroup));
	l+=sizeof(hostgroup);

	// write user
	strcpy(buf+l,user);
	l+=strlen(user);

	// write delimiter1
	strcpy(buf+l,_COMPUTE_HASH_DEL1_);
	l+=strlen(_COMPUTE_HASH_DEL1_);

	// write schema
	strcpy(buf+l,schema);
	l+=strlen(schema);

	// write delimiter2
	strcpy(buf+l,_COMPUTE_HASH_DEL2_);
	l+=strlen(_COMPUTE_HASH_DEL2_);

	// write query
	memcpy(buf+l,query,query_length);
	l+=query_length;

	uint64_t hash=SpookyHash::Hash64(buf,l,0);
	free(buf);
	return hash;
}


void MySQL_STMT_Global_info::compute_hash() {
	hash=stmt_compute_hash(hostgroup_id, username, schemaname, query, query_length);
}

uint64_t MySQL_STMTs_local::compute_hash(unsigned int hostgroup, char *user, char *schema, char *query, unsigned int query_length){
	uint64_t hash;
	hash=stmt_compute_hash(hostgroup, user, schema, query, query_length);
	return hash;
}

MySQL_STMTs_local::~MySQL_STMTs_local() {
	// Note: we do not free the prepared statements because we assume that
	// if we call this destructor the connection is being destroyed anyway
	for (std::map<uint32_t, MYSQL_STMT *>::iterator it=m.begin(); it!=m.end(); ++it) {
		uint32_t stmt_id=it->first;
		GloMyStmt->ref_count(stmt_id,-1);
	}
	m.erase(m.begin(),m.end());
}

MySQL_STMT_Manager::MySQL_STMT_Manager() {
	spinlock_rwlock_init(&rwlock);
	next_statement_id=1;	// we initialize this as 1 because we 0 is not allowed
}

MySQL_STMT_Manager::~MySQL_STMT_Manager() {
	for (std::map<uint32_t, MySQL_STMT_Global_info *>::iterator it=m.begin(); it!=m.end(); ++it) {
		MySQL_STMT_Global_info *a=it->second;
		delete a;
	}
	m.erase(m.begin(),m.end());
	// we do not loop in h because all the MySQL_STMT_Global_info() were already deleted
	h.erase(h.begin(),h.end());
}

int MySQL_STMT_Manager::ref_count(uint32_t statement_id, int cnt, bool lock) {
	int ret=-1;
	if (lock) {
		spin_wrlock(&rwlock);
	}
	auto s = m.find(statement_id);
	if (s!=m.end()) {
		MySQL_STMT_Global_info *a=s->second;
		a->ref_count+=cnt;
		ret=a->ref_count;
	}
	if (lock) {
		spin_wrunlock(&rwlock);
	}
	return ret;
}

uint32_t MySQL_STMT_Manager::add_prepared_statement(unsigned int _h, char *u, char *s, char *q, unsigned int ql, MYSQL_STMT *stmt, bool lock) {
	uint32_t ret=0;
	uint64_t hash=stmt_compute_hash(_h, u, s, q, ql); // this identifies the prepared statement
	if (lock) {
		spin_wrlock(&rwlock);
	}
	// try to find the statement
	auto f = h.find(hash);
	if (f!=h.end()) {
		// found it!
		MySQL_STMT_Global_info *a=f->second;
		ret=a->statement_id;
	} else {
		// we need to create a new one
		MySQL_STMT_Global_info *a=new MySQL_STMT_Global_info(next_statement_id,_h,u,s,q,ql,stmt,hash);
		// insert it in both maps
		m.insert(std::make_pair(a->statement_id, a));
		h.insert(std::make_pair(a->hash, a));
		ret=a->statement_id;
		next_statement_id++;	// increment it
	}

	if (lock) {
		spin_wrunlock(&rwlock);
	}
	return ret;
}


MySQL_STMT_Global_info * MySQL_STMT_Manager::find_prepared_statement_by_stmt_id(uint32_t id, bool lock) {
	MySQL_STMT_Global_info *ret=NULL; // assume we do not find it
	if (lock) {
		spin_wrlock(&rwlock);
	}

	auto s=m.find(id);
	if (s!=m.end()) {
		ret=s->second;
	}

	if (lock) {
		spin_wrunlock(&rwlock);
	}
	return ret;
}

MySQL_STMT_Global_info * MySQL_STMT_Manager::find_prepared_statement_by_hash(uint64_t hash, bool lock) {
	MySQL_STMT_Global_info *ret=NULL; // assume we do not find it
	if (lock) {
		spin_wrlock(&rwlock);
	}

	auto s=h.find(hash);
	if (s!=h.end()) {
		ret=s->second;
	}

	if (lock) {
		spin_wrunlock(&rwlock);
	}
	return ret;
}

