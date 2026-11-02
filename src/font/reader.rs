//! Bounds-checked big-endian binary reader used by all font table parsers.

use std::fmt;

/// Error raised while decoding font data.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FontError(pub String);

impl fmt::Display for FontError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "font error: {}", self.0)
    }
}

impl std::error::Error for FontError {}

pub type FontResult<T> = Result<T, FontError>;

pub(crate) fn err<T>(msg: impl Into<String>) -> FontResult<T> {
    Err(FontError(msg.into()))
}

/// A cursor over a byte slice. All reads are bounds checked.
#[derive(Clone, Copy)]
pub struct Reader<'a> {
    data: &'a [u8],
    pos: usize,
}

impl<'a> Reader<'a> {
    pub fn new(data: &'a [u8]) -> Self {
        Reader { data, pos: 0 }
    }

    /// A reader starting at `offset` of `data`.
    pub fn at(data: &'a [u8], offset: usize) -> FontResult<Self> {
        if offset > data.len() {
            return err(format!(
                "offset {offset} beyond end of data ({})",
                data.len()
            ));
        }
        Ok(Reader { data, pos: offset })
    }

    pub fn pos(&self) -> usize {
        self.pos
    }

    pub fn data(&self) -> &'a [u8] {
        self.data
    }

    pub fn seek(&mut self, pos: usize) -> FontResult<()> {
        if pos > self.data.len() {
            return err("seek beyond end of data");
        }
        self.pos = pos;
        Ok(())
    }

    pub fn skip(&mut self, n: usize) -> FontResult<()> {
        self.seek(self.pos + n)
    }

    pub fn bytes(&mut self, n: usize) -> FontResult<&'a [u8]> {
        let end = self
            .pos
            .checked_add(n)
            .ok_or_else(|| FontError("overflow".into()))?;
        if end > self.data.len() {
            return err(format!(
                "read of {n} bytes at {} beyond end ({})",
                self.pos,
                self.data.len()
            ));
        }
        let s = &self.data[self.pos..end];
        self.pos = end;
        Ok(s)
    }

    pub fn u8(&mut self) -> FontResult<u8> {
        Ok(self.bytes(1)?[0])
    }

    pub fn i8(&mut self) -> FontResult<i8> {
        Ok(self.u8()? as i8)
    }

    pub fn u16(&mut self) -> FontResult<u16> {
        let b = self.bytes(2)?;
        Ok(u16::from_be_bytes([b[0], b[1]]))
    }

    pub fn i16(&mut self) -> FontResult<i16> {
        Ok(self.u16()? as i16)
    }

    pub fn u24(&mut self) -> FontResult<u32> {
        let b = self.bytes(3)?;
        Ok(((b[0] as u32) << 16) | ((b[1] as u32) << 8) | b[2] as u32)
    }

    pub fn u32(&mut self) -> FontResult<u32> {
        let b = self.bytes(4)?;
        Ok(u32::from_be_bytes([b[0], b[1], b[2], b[3]]))
    }

    pub fn i32(&mut self) -> FontResult<i32> {
        Ok(self.u32()? as i32)
    }

    /// Read an unsigned integer of `size` bytes (1..=4), as used by CFF offsets.
    pub fn uint_n(&mut self, size: u8) -> FontResult<u32> {
        match size {
            1 => Ok(self.u8()? as u32),
            2 => Ok(self.u16()? as u32),
            3 => self.u24(),
            4 => self.u32(),
            _ => err(format!("invalid offset size {size}")),
        }
    }

    /// 16.16 fixed point.
    pub fn fixed(&mut self) -> FontResult<f64> {
        Ok(self.i32()? as f64 / 65536.0)
    }

    pub fn tag(&mut self) -> FontResult<[u8; 4]> {
        let b = self.bytes(4)?;
        Ok([b[0], b[1], b[2], b[3]])
    }
}

/// Read a u16 at an absolute offset.
pub fn u16_at(data: &[u8], off: usize) -> FontResult<u16> {
    Reader::at(data, off)?.u16()
}

/// Read an i16 at an absolute offset.
pub fn i16_at(data: &[u8], off: usize) -> FontResult<i16> {
    Reader::at(data, off)?.i16()
}

/// Big-endian writer helpers for building font programs.
pub fn push_u16(out: &mut Vec<u8>, v: u16) {
    out.extend_from_slice(&v.to_be_bytes());
}

pub fn push_u32(out: &mut Vec<u8>, v: u32) {
    out.extend_from_slice(&v.to_be_bytes());
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reads_big_endian_values() {
        let data = [0x01, 0x02, 0xFF, 0xFE, 0x00, 0x01, 0x00, 0x00];
        let mut r = Reader::new(&data);
        assert_eq!(r.u16().unwrap(), 0x0102);
        assert_eq!(r.i16().unwrap(), -2);
        assert_eq!(r.fixed().unwrap(), 1.0);
        assert!(r.u8().is_err());
    }

    #[test]
    fn uint_n_handles_all_sizes() {
        let data = [0x12, 0x34, 0x56, 0x78];
        assert_eq!(Reader::new(&data).uint_n(1).unwrap(), 0x12);
        assert_eq!(Reader::new(&data).uint_n(3).unwrap(), 0x123456);
        assert_eq!(Reader::new(&data).uint_n(4).unwrap(), 0x12345678);
        assert!(Reader::new(&data).uint_n(5).is_err());
    }
}
