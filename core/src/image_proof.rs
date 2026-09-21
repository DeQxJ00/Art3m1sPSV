//! Small, immutable alpha certificates tied to the decoded source payload.
//! They survive pixel demotion, but never apply to unrelated/dynamic uploads.
use crate::resource_ledger::{Charge,Owner,Tracked};
const HEADER:usize=32;
#[cfg(not(target_os="vita"))] const MAGIC:u32=0x31504641;

pub(crate) struct TileProof { width:u32,height:u32,cells:Tracked<Vec<u8>> }
impl TileProof {
    pub fn from_pixels(image:&image::RgbaImage)->Option<Self>{
        let (width,height)=image.dimensions();
        if width<960||height<540{return None;}
        let columns=(width as usize).checked_add(63)?/64;
        let rows=(height as usize).checked_add(63)?/64;
        let count=columns.checked_mul(rows)?.checked_add(HEADER)?;
        let mut charge=Charge::reserve(Owner::Decode,count);
        let mut cells=Vec::new();cells.try_reserve_exact(count).ok()?;
        charge.commit(cells.capacity());cells.resize(count,0);
        #[cfg(target_os="vita")]
        {
            unsafe extern "C" {fn art3m1s_gxm_prepare_opacity(width:u32,height:u32,pixels:*const u8,length:usize,cells:*mut u8,count:usize)->i32;}
            if unsafe{art3m1s_gxm_prepare_opacity(width,height,image.as_ptr(),image.len(),cells.as_mut_ptr(),count)}<=0{return None;}
        }
        #[cfg(not(target_os="vita"))]
        {
        for ty in 0..rows {for tx in 0..columns {
            cells[HEADER+ty*columns+tx]=(ty*64..((ty+1)*64).min(height as usize)).all(|y|
                (tx*64..((tx+1)*64).min(width as usize)).all(|x|image.as_raw()[(y*width as usize+x)*4+3]==255)) as u8;
        }}
        let (mut left,mut top,mut right,mut bottom)=(width,height,0,0);
        for (x,y,p) in image.enumerate_pixels(){if p[3]!=0{
            left=left.min(x);top=top.min(y);right=right.max(x+1);bottom=bottom.max(y+1);
        }}
        let opaque=cells[HEADER..].iter().all(|&c|c==1) as u32;
        for (i,v) in [MAGIC,width,height,left,top,right,bottom,opaque].into_iter().enumerate(){
            cells[i*4..i*4+4].copy_from_slice(&v.to_le_bytes());
        }
        }
        Some(Self{width,height,cells:Tracked{data:cells,charge}})
    }
    #[cfg(test)] pub fn for_size(&self,width:u32,height:u32)->Option<&[u8]>{
        (self.width==width&&self.height==height).then_some(&self.cells[HEADER..])
    }
    pub fn certificate_for_size(&self,width:u32,height:u32)->Option<&[u8]>{
        (self.width==width&&self.height==height).then_some(self.cells.as_slice())
    }
    pub fn opaque_for_size(&self,width:u32,height:u32)->Option<bool>{
        self.certificate_for_size(width,height).map(|c|c[28]==1)
    }
    pub fn bytes(&self)->usize{self.cells.capacity()}
    pub fn transfer(&mut self,owner:Owner){self.cells.transfer(owner);}
}
#[cfg(test)] mod tests {
    use super::*;
    #[test] fn bounds_and_opacity_are_bound_to_source_and_survive_owner_transfer(){
        let mut image=image::RgbaImage::from_pixel(960,540,image::Rgba([255,123,0,0]));
        let transparent=TileProof::from_pixels(&image).unwrap();
        let words=|p:&TileProof|p.certificate_for_size(960,540).unwrap()[..HEADER].chunks_exact(4)
            .map(|b|u32::from_le_bytes(b.try_into().unwrap())).collect::<Vec<_>>();
        assert_eq!(words(&transparent),[MAGIC,960,540,960,540,0,0,0]);
        image.put_pixel(121,317,image::Rgba([0,0,0,1]));
        let mut p=TileProof::from_pixels(&image).unwrap();let before=words(&p);
        assert_eq!(before,[MAGIC,960,540,121,317,122,318,0]);
        p.transfer(Owner::Ready);p.transfer(Owner::Provider);assert_eq!(words(&p),before);
        assert!(p.certificate_for_size(960,541).is_none());assert_eq!(p.opaque_for_size(960,540),Some(false));
        assert_eq!(p.bytes(),HEADER+15*9);
        assert_eq!(words(&transparent),[MAGIC,960,540,960,540,0,0,0]);
    }
    #[test] fn partial_tiles_and_alpha_not_rgb_determine_proof(){
        let mut image=image::RgbaImage::from_pixel(961,541,image::Rgba([0,128,254,255]));
        let all=TileProof::from_pixels(&image).unwrap();assert!(all.for_size(961,541).unwrap().iter().all(|&v|v==1));
        assert!(all.for_size(960,541).is_none());
        image.put_pixel(960,540,image::Rgba([255,255,255,254]));
        let p=TileProof::from_pixels(&image).unwrap();let c=p.for_size(961,541).unwrap();
        assert_eq!(c.len(),16*9);assert_eq!(c[c.len()-1],0);assert!(c[..c.len()-1].iter().all(|&v|v==1));
        image.put_pixel(0,0,image::Rgba([255,255,255,0]));
        assert_eq!(TileProof::from_pixels(&image).unwrap().for_size(961,541).unwrap()[0],0);
    }
}
