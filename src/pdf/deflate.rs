//! zlib / DEFLATE (RFC 1950/1951) compressor and decompressor.
//!
//! The compressor uses hash-chain LZ77 with lazy matching and chooses per
//! block between fixed and dynamic (length-limited) Huffman codes.

const WINDOW: usize = 32768;
const MIN_MATCH: usize = 3;
const MAX_MATCH: usize = 258;
const HASH_BITS: usize = 15;
const MAX_CHAIN: usize = 192;
const BLOCK_TOKENS: usize = 1 << 16;

const LEN_BASE: [u16; 29] = [
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131,
    163, 195, 227, 258,
];
const LEN_EXTRA: [u8; 29] = [
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0,
];
const DIST_BASE: [u16; 30] = [
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537,
    2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577,
];
const DIST_EXTRA: [u8; 30] = [
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13,
    13,
];
const CL_ORDER: [usize; 19] = [
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15,
];

#[derive(Clone, Copy)]
enum Token {
    Lit(u8),
    Match(u16, u16),
}

struct BitWriter {
    out: Vec<u8>,
    acc: u64,
    n: u32,
}

impl BitWriter {
    fn new() -> Self {
        BitWriter {
            out: Vec::new(),
            acc: 0,
            n: 0,
        }
    }
    fn bits(&mut self, v: u32, n: u32) {
        self.acc |= (v as u64) << self.n;
        self.n += n;
        while self.n >= 8 {
            self.out.push(self.acc as u8);
            self.acc >>= 8;
            self.n -= 8;
        }
    }
    /// Huffman codes are packed most-significant bit first.
    fn code(&mut self, code: u32, len: u8) {
        let mut rev = 0u32;
        for i in 0..len {
            rev |= ((code >> i) & 1) << (len - 1 - i);
        }
        self.bits(rev, len as u32);
    }
    fn finish(mut self) -> Vec<u8> {
        if self.n > 0 {
            self.out.push(self.acc as u8);
        }
        self.out
    }
}

fn len_code(len: u16) -> (usize, u32, u8) {
    let mut i = 28;
    while LEN_BASE[i] > len {
        i -= 1;
    }
    (257 + i, (len - LEN_BASE[i]) as u32, LEN_EXTRA[i])
}

fn dist_code(dist: u16) -> (usize, u32, u8) {
    let mut i = 29;
    while DIST_BASE[i] > dist {
        i -= 1;
    }
    (i, (dist - DIST_BASE[i]) as u32, DIST_EXTRA[i])
}

fn hash3(d: &[u8], i: usize) -> usize {
    let v = (d[i] as usize) << 10 ^ (d[i + 1] as usize) << 5 ^ d[i + 2] as usize;
    (v.wrapping_mul(2654435761)) >> (32 - HASH_BITS) & ((1 << HASH_BITS) - 1)
}

fn lz77(data: &[u8]) -> Vec<Token> {
    let n = data.len();
    let mut tokens = Vec::with_capacity(n / 2);
    let mut head = vec![usize::MAX; 1 << HASH_BITS];
    let mut prev = vec![usize::MAX; WINDOW];
    let insert = |head: &mut Vec<usize>, prev: &mut Vec<usize>, i: usize| {
        if i + MIN_MATCH <= n {
            let h = hash3(data, i);
            prev[i % WINDOW] = head[h];
            head[h] = i;
        }
    };
    let find = |head: &Vec<usize>, prev: &Vec<usize>, i: usize| -> (usize, usize) {
        if i + MIN_MATCH > n {
            return (0, 0);
        }
        let mut best_len = 0;
        let mut best_dist = 0;
        let mut cand = head[hash3(data, i)];
        let mut chain = 0;
        let max_len = MAX_MATCH.min(n - i);
        while cand != usize::MAX && chain < MAX_CHAIN {
            if cand >= i || i - cand > WINDOW - 1 {
                break;
            }
            if data[cand + best_len.min(max_len - 1)] == data[i + best_len.min(max_len - 1)] {
                let mut l = 0;
                while l < max_len && data[cand + l] == data[i + l] {
                    l += 1;
                }
                if l > best_len {
                    best_len = l;
                    best_dist = i - cand;
                    if l == max_len {
                        break;
                    }
                }
            }
            let p = prev[cand % WINDOW];
            if p == usize::MAX || p >= cand {
                break;
            }
            cand = p;
            chain += 1;
        }
        if best_len >= MIN_MATCH {
            (best_len, best_dist)
        } else {
            (0, 0)
        }
    };
    let mut i = 0;
    while i < n {
        let (len, dist) = find(&head, &prev, i);
        if len >= MIN_MATCH {
            // lazy evaluation: prefer a longer match starting at i+1
            insert(&mut head, &mut prev, i);
            let (len2, _) = if i + 1 < n {
                find(&head, &prev, i + 1)
            } else {
                (0, 0)
            };
            if len2 > len + 1 {
                tokens.push(Token::Lit(data[i]));
                i += 1;
                continue;
            }
            tokens.push(Token::Match(len as u16, dist as u16));
            for k in 1..len {
                insert(&mut head, &mut prev, i + k);
            }
            i += len;
        } else {
            insert(&mut head, &mut prev, i);
            tokens.push(Token::Lit(data[i]));
            i += 1;
        }
    }
    tokens
}

/// Length-limited Huffman code lengths (frequency halving when too deep).
fn huffman_lengths(freqs: &[u32], limit: u8) -> Vec<u8> {
    let mut f: Vec<u64> = freqs.iter().map(|&x| x as u64).collect();
    loop {
        let lens = huffman_unlimited(&f);
        if lens.iter().all(|&l| l <= limit) {
            return lens;
        }
        for x in f.iter_mut() {
            if *x > 0 {
                *x = (*x >> 1).max(1);
            }
        }
    }
}

fn huffman_unlimited(freqs: &[u64]) -> Vec<u8> {
    let n = freqs.len();
    let mut lens = vec![0u8; n];
    let used: Vec<usize> = (0..n).filter(|&i| freqs[i] > 0).collect();
    if used.is_empty() {
        return lens;
    }
    if used.len() == 1 {
        lens[used[0]] = 1;
        return lens;
    }
    // nodes: (freq, id); parents tracked to compute depth
    let mut parent: Vec<usize> = vec![usize::MAX; used.len() * 2];
    let mut heap: std::collections::BinaryHeap<std::cmp::Reverse<(u64, usize)>> =
        std::collections::BinaryHeap::new();
    for (k, &i) in used.iter().enumerate() {
        heap.push(std::cmp::Reverse((freqs[i], k)));
    }
    let mut next = used.len();
    while heap.len() > 1 {
        let std::cmp::Reverse((fa, a)) = heap.pop().unwrap();
        let std::cmp::Reverse((fb, b)) = heap.pop().unwrap();
        parent[a] = next;
        parent[b] = next;
        heap.push(std::cmp::Reverse((fa + fb, next)));
        next += 1;
    }
    for (k, &i) in used.iter().enumerate() {
        let mut d = 0u8;
        let mut p = k;
        while parent[p] != usize::MAX {
            p = parent[p];
            d = d.saturating_add(1);
        }
        lens[i] = d;
    }
    lens
}

fn canonical_codes(lens: &[u8]) -> Vec<u32> {
    let max = *lens.iter().max().unwrap_or(&0) as usize;
    let mut bl_count = vec![0u32; max + 2];
    for &l in lens {
        if l > 0 {
            bl_count[l as usize] += 1;
        }
    }
    let mut next = vec![0u32; max + 2];
    let mut code = 0u32;
    for bits in 1..=max {
        code = (code + bl_count[bits - 1]) << 1;
        next[bits] = code;
    }
    lens.iter()
        .map(|&l| {
            if l == 0 {
                0
            } else {
                let c = next[l as usize];
                next[l as usize] += 1;
                c
            }
        })
        .collect()
}

fn fixed_lengths() -> (Vec<u8>, Vec<u8>) {
    let mut lit = vec![0u8; 288];
    for (i, l) in lit.iter_mut().enumerate() {
        *l = match i {
            0..=143 => 8,
            144..=255 => 9,
            256..=279 => 7,
            _ => 8,
        };
    }
    (lit, vec![5u8; 30])
}

fn block_cost(tokens: &[Token], lit: &[u8], dist: &[u8]) -> u64 {
    let mut bits = lit[256] as u64;
    for t in tokens {
        match *t {
            Token::Lit(b) => bits += lit[b as usize] as u64,
            Token::Match(l, d) => {
                let (lc, _, le) = len_code(l);
                let (dc, _, de) = dist_code(d);
                bits += lit[lc] as u64 + le as u64 + dist[dc] as u64 + de as u64;
            }
        }
    }
    bits
}

fn write_tokens(w: &mut BitWriter, tokens: &[Token], lit: &[u8], dist: &[u8]) {
    let lc = canonical_codes(lit);
    let dc = canonical_codes(dist);
    for t in tokens {
        match *t {
            Token::Lit(b) => w.code(lc[b as usize], lit[b as usize]),
            Token::Match(l, d) => {
                let (c, e, eb) = len_code(l);
                w.code(lc[c], lit[c]);
                if eb > 0 {
                    w.bits(e, eb as u32);
                }
                let (c, e, eb) = dist_code(d);
                w.code(dc[c], dist[c]);
                if eb > 0 {
                    w.bits(e, eb as u32);
                }
            }
        }
    }
    w.code(lc[256], lit[256]);
}

/// Run-length encode code lengths with symbols 16/17/18.
fn rle_lengths(lens: &[u8]) -> Vec<(u8, u8)> {
    let mut out = Vec::new();
    let mut i = 0;
    while i < lens.len() {
        let l = lens[i];
        let mut run = 1;
        while i + run < lens.len() && lens[i + run] == l {
            run += 1;
        }
        if l == 0 && run >= 3 {
            let mut r = run;
            while r >= 3 {
                if r >= 11 {
                    let k = r.min(138);
                    out.push((18, (k - 11) as u8));
                    r -= k;
                } else {
                    let k = r.min(10);
                    out.push((17, (k - 3) as u8));
                    r -= k;
                }
            }
            for _ in 0..r {
                out.push((0, 0));
            }
            i += run;
        } else if l != 0 && run >= 4 {
            out.push((l, 0));
            let mut r = run - 1;
            while r >= 3 {
                let k = r.min(6);
                out.push((16, (k - 3) as u8));
                r -= k;
            }
            for _ in 0..r {
                out.push((l, 0));
            }
            i += run;
        } else {
            out.push((l, 0));
            i += 1;
        }
    }
    out
}

fn write_block(w: &mut BitWriter, tokens: &[Token], last: bool) {
    let mut lf = vec![0u32; 286];
    let mut df = vec![0u32; 30];
    lf[256] = 1;
    for t in tokens {
        match *t {
            Token::Lit(b) => lf[b as usize] += 1,
            Token::Match(l, d) => {
                lf[len_code(l).0] += 1;
                df[dist_code(d).0] += 1;
            }
        }
    }
    let mut lit = huffman_lengths(&lf, 15);
    let mut dist = huffman_lengths(&df, 15);
    if dist.iter().all(|&l| l == 0) {
        dist[0] = 1;
    }
    let hlit = (257..=286)
        .rev()
        .find(|&n| lit[n - 1] != 0)
        .unwrap_or(257)
        .max(257);
    let hdist = (1..=30)
        .rev()
        .find(|&n| dist[n - 1] != 0)
        .unwrap_or(1)
        .max(1);
    lit.truncate(hlit);
    dist.truncate(hdist);
    let mut all = lit.clone();
    all.extend_from_slice(&dist);
    let rle = rle_lengths(&all);
    let mut cf = vec![0u32; 19];
    for &(s, _) in &rle {
        cf[s as usize] += 1;
    }
    let cl = huffman_lengths(&cf, 7);
    let hclen = (4..=19)
        .rev()
        .find(|&n| cl[CL_ORDER[n - 1]] != 0)
        .unwrap_or(4)
        .max(4);

    let mut dyn_bits = 3 + 5 + 5 + 4 + 3 * hclen as u64;
    for &(s, _) in &rle {
        dyn_bits += cl[s as usize] as u64
            + match s {
                16 => 2,
                17 => 3,
                18 => 7,
                _ => 0,
            };
    }
    let mut lit_full = lit.clone();
    lit_full.resize(286, 0);
    let mut dist_full = dist.clone();
    dist_full.resize(30, 0);
    dyn_bits += block_cost(tokens, &lit_full, &dist_full);
    let (fl, fd) = fixed_lengths();
    let fixed_bits = 3 + block_cost(tokens, &fl, &fd);

    if fixed_bits <= dyn_bits {
        w.bits(last as u32, 1);
        w.bits(1, 2);
        write_tokens(w, tokens, &fl, &fd);
    } else {
        w.bits(last as u32, 1);
        w.bits(2, 2);
        w.bits((hlit - 257) as u32, 5);
        w.bits((hdist - 1) as u32, 5);
        w.bits((hclen - 4) as u32, 4);
        for &idx in CL_ORDER.iter().take(hclen) {
            w.bits(cl[idx] as u32, 3);
        }
        let cc = canonical_codes(&cl);
        for &(s, e) in &rle {
            w.code(cc[s as usize], cl[s as usize]);
            match s {
                16 => w.bits(e as u32, 2),
                17 => w.bits(e as u32, 3),
                18 => w.bits(e as u32, 7),
                _ => {}
            }
        }
        write_tokens(w, tokens, &lit_full, &dist_full);
    }
}

pub fn adler32(data: &[u8]) -> u32 {
    let (mut a, mut b) = (1u32, 0u32);
    for chunk in data.chunks(5552) {
        for &x in chunk {
            a += x as u32;
            b += a;
        }
        a %= 65521;
        b %= 65521;
    }
    (b << 16) | a
}

/// Compress into a zlib stream (suitable for PDF `/FlateDecode`).
pub fn zlib_compress(data: &[u8]) -> Vec<u8> {
    let tokens = lz77(data);
    let mut w = BitWriter::new();
    w.out.extend_from_slice(&[0x78, 0xDA]);
    if tokens.is_empty() {
        w.bits(1, 1);
        w.bits(1, 2);
        let (fl, fd) = fixed_lengths();
        write_tokens(&mut w, &[], &fl, &fd);
    } else {
        let chunks: Vec<&[Token]> = tokens.chunks(BLOCK_TOKENS).collect();
        for (k, c) in chunks.iter().enumerate() {
            write_block(&mut w, c, k + 1 == chunks.len());
        }
    }
    let mut out = w.finish();
    out.extend_from_slice(&adler32(data).to_be_bytes());
    out
}

/// Decompress a zlib stream.
pub fn zlib_decompress(data: &[u8]) -> Result<Vec<u8>, String> {
    if data.len() < 6 {
        return Err("zlib stream too short".into());
    }
    if (data[0] as u16 * 256 + data[1] as u16) % 31 != 0 || data[0] & 0x0F != 8 {
        return Err("bad zlib header".into());
    }
    let out = inflate(&data[2..])?;
    Ok(out)
}

struct BitReader<'a> {
    d: &'a [u8],
    pos: usize,
    bit: u32,
}

impl BitReader<'_> {
    fn bit(&mut self) -> Result<u32, String> {
        let byte = *self
            .d
            .get(self.pos)
            .ok_or("unexpected end of deflate data")?;
        let v = (byte >> self.bit) & 1;
        self.bit += 1;
        if self.bit == 8 {
            self.bit = 0;
            self.pos += 1;
        }
        Ok(v as u32)
    }
    fn bits(&mut self, n: u32) -> Result<u32, String> {
        let mut v = 0;
        for i in 0..n {
            v |= self.bit()? << i;
        }
        Ok(v)
    }
    fn align(&mut self) {
        if self.bit != 0 {
            self.bit = 0;
            self.pos += 1;
        }
    }
}

struct Decoder {
    counts: [u16; 16],
    symbols: Vec<u16>,
}

impl Decoder {
    fn new(lens: &[u8]) -> Decoder {
        let mut counts = [0u16; 16];
        for &l in lens {
            counts[l as usize] += 1;
        }
        counts[0] = 0;
        let mut offs = [0u16; 16];
        for i in 1..16 {
            offs[i] = offs[i - 1] + counts[i - 1];
        }
        let mut symbols = vec![0u16; lens.len()];
        for (s, &l) in lens.iter().enumerate() {
            if l != 0 {
                symbols[offs[l as usize] as usize] = s as u16;
                offs[l as usize] += 1;
            }
        }
        Decoder { counts, symbols }
    }
    fn decode(&self, r: &mut BitReader) -> Result<u16, String> {
        let (mut code, mut first, mut index) = (0i32, 0i32, 0i32);
        for len in 1..16 {
            code |= r.bit()? as i32;
            let count = self.counts[len] as i32;
            if code - count < first {
                return Ok(self.symbols[(index + (code - first)) as usize]);
            }
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        Err("invalid Huffman code".into())
    }
}

/// Raw DEFLATE decoder.
pub fn inflate(data: &[u8]) -> Result<Vec<u8>, String> {
    let mut r = BitReader {
        d: data,
        pos: 0,
        bit: 0,
    };
    let mut out: Vec<u8> = Vec::new();
    loop {
        let last = r.bit()?;
        let kind = r.bits(2)?;
        match kind {
            0 => {
                r.align();
                if r.pos + 4 > data.len() {
                    return Err("truncated stored block".into());
                }
                let len = u16::from_le_bytes([data[r.pos], data[r.pos + 1]]) as usize;
                r.pos += 4;
                if r.pos + len > data.len() {
                    return Err("truncated stored block".into());
                }
                out.extend_from_slice(&data[r.pos..r.pos + len]);
                r.pos += len;
            }
            1 | 2 => {
                let (lit, dist) = if kind == 1 {
                    let (l, d) = fixed_lengths();
                    (Decoder::new(&l), Decoder::new(&d))
                } else {
                    let hlit = r.bits(5)? as usize + 257;
                    let hdist = r.bits(5)? as usize + 1;
                    let hclen = r.bits(4)? as usize + 4;
                    let mut cl = [0u8; 19];
                    for &idx in CL_ORDER.iter().take(hclen) {
                        cl[idx] = r.bits(3)? as u8;
                    }
                    let cd = Decoder::new(&cl);
                    let mut lens = Vec::with_capacity(hlit + hdist);
                    while lens.len() < hlit + hdist {
                        let s = cd.decode(&mut r)?;
                        match s {
                            0..=15 => lens.push(s as u8),
                            16 => {
                                let p = *lens.last().ok_or("repeat without previous length")?;
                                for _ in 0..3 + r.bits(2)? {
                                    lens.push(p);
                                }
                            }
                            17 => {
                                for _ in 0..3 + r.bits(3)? {
                                    lens.push(0);
                                }
                            }
                            _ => {
                                for _ in 0..11 + r.bits(7)? {
                                    lens.push(0);
                                }
                            }
                        }
                    }
                    (Decoder::new(&lens[..hlit]), Decoder::new(&lens[hlit..]))
                };
                loop {
                    let s = lit.decode(&mut r)? as usize;
                    if s < 256 {
                        out.push(s as u8);
                    } else if s == 256 {
                        break;
                    } else {
                        let i = s - 257;
                        if i >= 29 {
                            return Err("bad length code".into());
                        }
                        let len = LEN_BASE[i] as usize + r.bits(LEN_EXTRA[i] as u32)? as usize;
                        let dc = dist.decode(&mut r)? as usize;
                        if dc >= 30 {
                            return Err("bad distance code".into());
                        }
                        let d = DIST_BASE[dc] as usize + r.bits(DIST_EXTRA[dc] as u32)? as usize;
                        if d > out.len() {
                            return Err("distance too far back".into());
                        }
                        let start = out.len() - d;
                        for k in 0..len {
                            out.push(out[start + k]);
                        }
                    }
                }
            }
            _ => return Err("invalid block type".into()),
        }
        if last == 1 {
            break;
        }
    }
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn roundtrip(data: &[u8]) {
        let c = zlib_compress(data);
        let d = zlib_decompress(&c).expect("valid stream");
        assert_eq!(d, data);
        assert_eq!(
            adler32(&d),
            u32::from_be_bytes([
                c[c.len() - 4],
                c[c.len() - 3],
                c[c.len() - 2],
                c[c.len() - 1]
            ])
        );
    }

    #[test]
    fn roundtrips_various_inputs() {
        roundtrip(b"");
        roundtrip(b"a");
        roundtrip(b"abcabcabcabcabcabcabcabc");
        let text = "BT /F1 12 Tf 72 720 Td (Hello, world) Tj ET\n".repeat(500);
        roundtrip(text.as_bytes());
        let mut x = 12345u32;
        let noise: Vec<u8> = (0..70000)
            .map(|_| {
                x ^= x << 13;
                x ^= x >> 17;
                x ^= x << 5;
                (x % 7) as u8 + b'a'
            })
            .collect();
        roundtrip(&noise);
    }

    #[test]
    fn compresses_repetitive_content() {
        let text = "q 1 0 0 1 72 700 cm 0 0 m 100 0 l S Q\n".repeat(1000);
        let c = zlib_compress(text.as_bytes());
        assert!(c.len() < text.len() / 20, "{} vs {}", c.len(), text.len());
    }
}
