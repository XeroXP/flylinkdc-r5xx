#ifndef DCPLUSPLUS_DCPP_SSL_H
#define DCPLUSPLUS_DCPP_SSL_H

#include "typedefs.h"

#include <openssl/ssl.h>
#include <vector>
#include <cstdint>

namespace ssl
{

template<typename T, void (*Release)(T*)>
class scoped_handle
{
	public:
		explicit scoped_handle(T* t_ = nullptr) : t(t_) { }
		~scoped_handle()
		{
			Release(t);
		}
		
		operator T*()
		{
			return t;
		}
		operator const T*() const
		{
			return t;
		}
		
		T* operator->()
		{
			return t;
		}
		const T* operator->() const
		{
			return t;
		}
		
		void reset(T* t_ = nullptr)
		{
			Release(t);
			t = t_;
		}
	private:
		scoped_handle(const scoped_handle<T, Release>&);
		scoped_handle<T, Release>& operator=(const scoped_handle<T, Release>&);
		
		T* t;
};

typedef scoped_handle<SSL, SSL_free> SSL;
typedef scoped_handle<SSL_CTX, SSL_CTX_free> SSL_CTX;

typedef scoped_handle<ASN1_INTEGER, ASN1_INTEGER_free> ASN1_INTEGER;
typedef scoped_handle<BIGNUM, BN_free> BIGNUM;
typedef scoped_handle<DH, DH_free> DH;

typedef scoped_handle<DSA, DSA_free> DSA;
typedef scoped_handle<EVP_PKEY, EVP_PKEY_free> EVP_PKEY;
typedef scoped_handle<RSA, RSA_free> RSA;
typedef scoped_handle<X509, X509_free> X509;
typedef scoped_handle<X509_NAME, X509_NAME_free> X509_NAME;

ByteVector X509_digest(::X509* x509, const ::EVP_MD* md);


}

#endif /*DCPLUSPLUS_DCPP_SSL_H*/
