#include <assert.h>
#include <vector>
#include <iomanip>
#include <crypto/cbc.hh>
#include <crypto/cmc.hh>
#include <crypto/prng.hh>
#include <crypto/aes.hh>
#include <crypto/blowfish.hh>
#include <crypto/ope.hh>
#include <crypto/arc4.hh>
#include <crypto/hgd.hh>
#include <crypto/sha.hh>
#include <crypto/hmac.hh>
#include <crypto/paillier.hh>
#include <crypto/bn.hh>
#include <crypto/ecjoin.hh>
#include <crypto/search.hh>
#include <crypto/skip32.hh>
#include <crypto/cbcmac.hh>
#include <crypto/ffx.hh>
#include <util/timer.hh>
#include <NTL/ZZ.h>
#include <NTL/RR.h>

using namespace std;
using namespace NTL;

template<class T>
void
test_block_cipher(T *c, PRNG *u, const std::string &cname)
{
    auto pt = u->rand_vec<uint8_t>(c->blocksize);
    vector<uint8_t> ct(pt.size()), pt2(pt.size());

    c->block_encrypt(&pt[0], &ct[0]);
    c->block_decrypt(&ct[0], &pt2[0]);
    assert(pt == pt2);

    auto cbc_pt = u->rand_vec<uint8_t>(c->blocksize * 32);
    auto cbc_iv = u->rand_vec<uint8_t>(c->blocksize);
    vector<uint8_t> cbc_ct, cbc_pt2;
    cbc_encrypt(c, cbc_iv, cbc_pt, &cbc_ct);
    cbc_decrypt(c, cbc_iv, cbc_ct, &cbc_pt2);
    assert(cbc_pt == cbc_pt2);

    cmc_encrypt(c, cbc_pt, &cbc_ct);
    cmc_decrypt(c, cbc_ct, &cbc_pt2);
    assert(cbc_pt == cbc_pt2);

    enum { nperf = 1000 };
    auto cbc_perf_pt = u->rand_vec<uint8_t>(1024);
    auto cbc_perf_iv = u->rand_vec<uint8_t>(c->blocksize);
    vector<uint8_t> cbc_perf_ct, cbc_perf_pt2;
    timer cbc_perf;
    for (uint i = 0; i < nperf; i++) {
        cbc_encrypt(c, cbc_perf_iv, cbc_perf_pt, &cbc_perf_ct);
        if (i == 0) {
            cbc_decrypt(c, cbc_perf_iv, cbc_perf_ct, &cbc_perf_pt2);
            assert(cbc_perf_pt == cbc_perf_pt2);
        }
    }

    cout << cname << "-cbc speed: "
         << cbc_perf_pt.size() * nperf * 1000 * 1000 / cbc_perf.lap() << endl;

    timer cmc_perf;
    for (uint i = 0; i < nperf; i++) {
        cmc_encrypt(c, cbc_perf_pt, &cbc_perf_ct);
        if (i == 0) {
            cmc_decrypt(c, cbc_perf_ct, &cbc_perf_pt2);
            assert(cbc_perf_pt == cbc_perf_pt2);
        }
    }

    cout << cname << "-cmc speed: "
         << cbc_perf_pt.size() * nperf * 1000 * 1000 / cmc_perf.lap() << endl;
}

static void
test_ope(int pbits, int cbits)
{
    urandom u;
    OPE o("hello world", pbits, cbits);
    RR maxerr = to_RR(0);

    timer t;
    enum { niter = 100 };
    for (uint i = 1; i < niter; i++) {
        ZZ pt = u.rand_zz_mod(to_ZZ(1) << pbits);
        ZZ ct = o.encrypt(pt);
        ZZ pt2 = o.decrypt(ct);
        assert(pt2 == pt);
        // cout << pt << " -> " << o.encrypt(pt, -1) << "/" << ct << "/" << o.encrypt(pt, 1) << " -> " << pt2 << endl;

        RR::SetPrecision(cbits+pbits);
        ZZ guess = ct / (to_ZZ(1) << (cbits-pbits));
        RR error = abs(to_RR(guess) / to_RR(pt) - 1);
        maxerr = max(error, maxerr);
        // cout << "pt guess is " << error << " off" << endl;
    }
    cout << "--- ope: " << pbits << "-bit plaintext, "
         << cbits << "-bit ciphertext" << endl
         << "  enc/dec pair: " << t.lap() / niter << " usec; "
         << "~#bits leaked: "
           << ((maxerr < pow(to_RR(2), to_RR(-pbits))) ? pbits
                                                       : NumBits(to_ZZ(1/maxerr))) << endl;
}

static void
test_hgd()
{
    streamrng<arc4> r("hello world");
    ZZ s;

    s = HGD(to_ZZ(100), to_ZZ(100), to_ZZ(100), &r);
    assert(s > 0 && s < 100);

    s = HGD(to_ZZ(100), to_ZZ(0), to_ZZ(100), &r);
    assert(s == 0);

    s = HGD(to_ZZ(100), to_ZZ(100), to_ZZ(0), &r);
    assert(s == 100);
}

static void
test_paillier()
{
    auto sk = Paillier_priv::keygen();
    Paillier_priv pp(sk);

    auto pk = pp.pubkey();
    Paillier p(pk);

    urandom u;
    ZZ pt0 = u.rand_zz_mod(to_ZZ(1) << 256);
    ZZ pt1 = u.rand_zz_mod(to_ZZ(1) << 256);

    ZZ ct0 = p.encrypt(pt0);
    ZZ ct1 = p.encrypt(pt1);
    ZZ sum = p.add(ct0, ct1);
    assert(pp.decrypt(ct0) == pt0);
    assert(pp.decrypt(ct1) == pt1);
    assert(pp.decrypt(sum) == (pt0 + pt1));
}

static void
test_bn()
{
    bignum a(123);
    bignum b(20);
    bignum c(78);
    bignum d(500);

    auto r = (a + b * c) % d;
    assert(r == 183);
    assert(r <= 183);
    assert(r <= 184);
    assert(r <  184);
    assert(r >= 183);
    assert(r >= 181);
    assert(r >  181);

    streamrng<arc4> rand("seed");
    assert(rand.rand_bn_mod(1000) == 498);
}

static void
test_ecjoin()
{
    ecjoin_priv e("hello world");

    auto p1 = e.hash("some data", "hash key");
    auto p2 = e.hash("some data", "hash key");
    assert(p1 == p2);

    auto p3 = e.hash("some data", "another hash key");
    auto p4 = e.hash("other data", "hash key");
    assert(p1 != p4);
    assert(p3 != p4);

    bignum d = e.delta("another hash key", "hash key");
    auto p5 = e.adjust(p3, d);
    assert(p1 == p5);
}

static void
test_search()
{
    search_priv s("my key");

    auto cl = s.transform({"hello", "world", "hello", "testing", "test"});
    assert(s.match(cl, s.wordkey("hello")));
    assert(!s.match(cl, s.wordkey("Hello")));
    assert(s.match(cl, s.wordkey("world")));
}

static void
test_skip32(void)
{
    std::vector<uint8_t> k = { 0x00, 0x99, 0x88, 0x77, 0x66,
                               0x55, 0x44, 0x33, 0x22, 0x11 };
    skip32 s(k);

    uint8_t pt[4] = { 0x33, 0x22, 0x11, 0x00 };
    uint8_t ct[4];
    s.block_encrypt(pt, ct);
    assert(ct[0] == 0x81 && ct[1] == 0x9d && ct[2] == 0x5f && ct[3] == 0x1f);

    uint8_t pt2[4];
    s.block_decrypt(ct, pt2);
    assert(pt2[0] == 0x33 && pt2[1] == 0x22 && pt2[2] == 0x11 && pt2[3] == 0x00);
}

static void
test_ffx()
{
    streamrng<arc4> rnd("test seed");

    AES key(rnd.rand_vec<uint8_t>(16));

    for (int i = 0; i < 100; i++) {
        uint nbits = 8 + (rnd.rand<uint>() % 121);
        auto pt = rnd.rand_vec<uint8_t>((nbits + 7) / 8);
        auto t = rnd.rand_vec<uint8_t>(rnd.rand<uint>() % 1024);

        uint lowbits = nbits % 8;
        pt[(nbits-1) / 8] &= ~0 << (8 - lowbits);

        std::vector<uint8_t> ct, pt2;
        ct.resize(pt.size());
        pt2.resize(pt.size());

        ffx_a2_inited f0(&key, nbits, t);
        f0.encrypt(&pt[0], &ct[0]);

        ffx_a2_inited f1(&key, nbits, t);   /* duplicate of f0, for testing */
        f1.decrypt(&ct[0], &pt2[0]);

        if (0) {
            cout << "nbits: " << nbits << endl;

            cout << "plaintext:  ";
            for (auto &x: pt)
                cout << hex << setw(2) << setfill('0') << (uint) x;
            cout << dec << endl;

            cout << "ciphertext: ";
            for (auto &x: ct)
                cout << hex << setw(2) << setfill('0') << (uint) x;
            cout << dec << endl;

            cout << "plaintext2: ";
            for (auto &x: pt2)
                cout << hex << setw(2) << setfill('0') << (uint) x;
            cout << dec << endl;
        }

        assert(pt != ct);
        assert(pt == pt2);
    }

    urandom u;
    auto tweak = u.rand_vec<uint8_t>(1024);

    ffx_a2_block_cipher<128> fbc128(&key, tweak);
    test_block_cipher(&fbc128, &u, "ffx128-aes128");

    ffx_a2_block_cipher<64> fbc64(&key, tweak);
    test_block_cipher(&fbc64, &u, "ffx64-aes128");

    ffx_a2_block_cipher<32> fbc32(&key, tweak);
    test_block_cipher(&fbc32, &u, "ffx32-aes128");

    ffx_a2_block_cipher<16> fbc16(&key, tweak);
    test_block_cipher(&fbc16, &u, "ffx16-aes128");

    ffx_a2_block_cipher<8> fbc8(&key, tweak);
    test_block_cipher(&fbc8, &u, "ffx8-aes128");
}

int
main(int ac, char **av)
{
    urandom u;
    cout << u.rand<uint64_t>() << endl;
    cout << u.rand<int64_t>() << endl;

    test_bn();
    test_ecjoin();
    test_search();
    test_paillier();
    test_skip32();
    test_ffx();

    AES aes128(u.rand_vec<uint8_t>(16));
    test_block_cipher(&aes128, &u, "aes-128");

    AES aes256(u.rand_vec<uint8_t>(32));
    test_block_cipher(&aes256, &u, "aes-256");

    blowfish bf(u.rand_vec<uint8_t>(128));
    test_block_cipher(&bf, &u, "blowfish");

    skip32 s32(u.rand_vec<uint8_t>(10));
    test_block_cipher(&s32, &u, "skip32");

    auto hv = sha256::hash("Hello world\n");
    for (auto &x: hv)
        cout << hex << setw(2) << setfill('0') << (uint) x;
    cout << dec << endl;

    auto mv = hmac<sha256>::mac("Hello world\n", "key");
    for (auto &x: mv)
        cout << hex << setw(2) << setfill('0') << (uint) x;
    cout << dec << endl;

    cbcmac<AES> cmac(&aes256);
    cmac.update(sha256::hash("Hello world\n"));
    for (auto &x: cmac.final())
        cout << hex << setw(2) << setfill('0') << (uint) x;
    cout << dec << endl;

    test_hgd();

    for (int pbits = 32; pbits <= 128; pbits += 32)
        for (int cbits = pbits; cbits <= pbits + 128; cbits += 32)
            test_ope(pbits, cbits);
}
