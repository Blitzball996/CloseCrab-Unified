/* Glue for the vendored TweetNaCl (public domain).
 *
 * TweetNaCl references randombytes() from its key-generation paths
 * (crypto_box_keypair / crypto_sign_keypair). We ONLY ever call
 * crypto_sign_open() (signature verification), which never touches
 * randombytes — but the symbol must still resolve at link time.
 *
 * If it is ever called we abort loudly: this binary has no business
 * generating keys (the private key lives only on the server).
 */
#include <stdlib.h>

void randombytes(unsigned char *x, unsigned long long n) {
    (void)x;
    (void)n;
    abort();
}
