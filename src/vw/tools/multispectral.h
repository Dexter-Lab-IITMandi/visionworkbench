// __BEGIN_LICENSE__
//  Copyright (c) 2006-2013, United States Government as represented by the
//  Administrator of the National Aeronautics and Space Administration. All
//  rights reserved.
//
//  The NASA Vision Workbench is licensed under the Apache License,
//  Version 2.0 (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//  http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.
// __END_LICENSE__


#ifndef __VW_MULTISPECTRAL_H__
#define __VW_MULTISPECTRAL_H__

#include <stdlib.h>
#include <algorithm>
#include <vw/Image/PixelMask.h>
#include <vw/Image/Manipulation.h>
#include <vw/Image/Transform.h>
#include <vw/FileIO/DiskImageView.h>
#include <vw/Cartography/GeoReferenceUtils.h>
#include <vw/tools/flood_common.h>

/**
  Tools for processing multispectral image data.
*/

namespace vw {

namespace multispectral {


// Multispectral image types

// TODO: Operate on WV2 images also!

const int NUM_SPOT67_BANDS    = 5;
const int NUM_WORLDVIEW_BANDS = 8;

/// Band-averaged solar spectral irradiance
/// - Copied from "Radiometric Use of WorldView-2 Imagery"
const float WORLDVIEW_ESUN[NUM_WORLDVIEW_BANDS] = {
  //1580.8140, // PAN
  1758.2229, // Coastal
  1974.2416, // Blue
  1856.4104, // Green
  1738.4791, // Yellow
  1559.4555, // Red
  1342.0695, // Red Edge
  1069.7302, // NIR 1
  861.2866   // NIR 2
};

enum SPOT67_CHANNEL_INDICES { SPOT_PAN   = 0, 
                              SPOT_BLUE  = 1, 
                              SPOT_GREEN = 2, 
                              SPOT_RED   = 3, 
                              SPOT_NIR   = 4};

enum WORLDVIEW3_CHANNEL_INDICES { COASTAL  = 0, 
                                  BLUE     = 1, 
                                  GREEN    = 2, 
                                  YELLOW   = 3, 
                                  RED      = 4, 
                                  RED_EDGE = 5,
                                  NIR1     = 6,
                                  NIR2     = 7};

// 
typedef PixelMask<Vector<uint8,  NUM_SPOT67_BANDS   > > Spot67PixelType;
typedef PixelMask<Vector<uint16, NUM_WORLDVIEW_BANDS> > WorldView3PixelType;
typedef PixelMask<Vector<float,  NUM_SPOT67_BANDS   > > Spot67ToaPixelType;
typedef PixelMask<Vector<float,  NUM_WORLDVIEW_BANDS> > WorldView3ToaPixelType;

// We use Refs for these types in case the images are very large.
typedef ImageViewRef<Spot67PixelType> Spot67Image;
typedef ImageViewRef<WorldView3PixelType> WorldView3Image;

/// Loads an image from either Spot6 or Spot7 (they are the same format)
void load_spot67_image(std::vector<std::string> const& input_paths,
                       Spot67Image & image,
                       cartography::GeoReference &georef) {
  
  std::string image_path = find_string_in_list(input_paths, ".tif");
  if (image_path.empty())
    vw_throw( ArgumentErr() << "Error: SPOT image file not found!\n");
    
  // TODO: Is zero always the nodata value?
  image = create_mask(DiskImageView<Vector<uint8, NUM_SPOT67_BANDS> >(image_path));
  
  DiskImageResourceGDAL disk_resource(image_path);
  if (!read_georeference(georef, disk_resource)) 
    vw_throw( ArgumentErr() << "Failed to read georeference from image " << image_path <<"\n");
}


/// Load a Worldview 3 multispectral image
void load_worldview3_image(std::vector<std::string> const& input_paths,
                           WorldView3Image & image,
                           cartography::GeoReference &georef) {

  // Find the image file
  std::string image_path = find_string_in_list(input_paths, ".tif");
  if (image_path.empty())
    vw_throw( ArgumentErr() << "Error: WorldView image file not found!\n");

  // Load 8 bands from one image
  // - The band order is Coastal, Blue, Green, Yellow, Red, Red-Edge, Near-IR1, Near-IR2

  // TODO: Is zero the standard nodata value?
  // - The image is uint16 but only 11 bits are used (max value 2047)
  typedef DiskImageView<uint16> WvDiskView;
  image = create_mask(planes_to_channels<Vector<uint16, NUM_WORLDVIEW_BANDS>,  WvDiskView>(WvDiskView(image_path)));

  DiskImageResourceGDAL disk_resource(image_path);
  if (!read_georeference(georef, disk_resource)) 
    vw_throw( ArgumentErr() << "Failed to read georeference from image " << image_path <<"\n");
}
/*
void read_spot67_metadata() {

TODO: Where are the gain/offset values we need for each band to convert to TOA??

}
*/
/// Convenience structure for storing Landsat metadata information
struct WorldViewMetadataContainer {

  typedef Vector<float, NUM_WORLDVIEW_BANDS> CoefficientVector;
  
  CoefficientVector abs_cal_factor;
  CoefficientVector effective_bandwidth;
  float             mean_sun_elevation; // Units = degrees
  float             earth_sun_distance; // Units = AU
  std::string       datetime;
  
  /// Populate derived values from input values
  void populate_derived_values() {
    // Extract numbers
    // - Input format: "2016-10-23T17:46:54.796950Z;"
    int   year   = atoi(datetime.substr( 0,4).c_str());
    int   month  = atoi(datetime.substr( 5,2).c_str());
    int   day    = atoi(datetime.substr( 8,2).c_str());
    int   hour   = atoi(datetime.substr(11,2).c_str());
    int   minute = atoi(datetime.substr(14,2).c_str());
    float second = atof(datetime.substr(17,8).c_str());
    
    earth_sun_distance = compute_earth_sun_distance(year, month, day, hour, minute, second);
  }
};


void load_worldview3_metadata(std::vector<std::string> const& input_paths,
                              WorldViewMetadataContainer &metadata) {
  // Find the metadata file
  std::string metadata_path = find_string_in_list(input_paths, ".IMD");
  if (metadata_path.empty())
    vw_throw( ArgumentErr() << "Error: WorldView metadata file not found!\n");
    
  // Search the file for the metadata
  std::ifstream handle(metadata_path.c_str());
  std::string line;
  int channel_index = -1, found_count = 0;
  while (std::getline(handle, line)) {
  
    // Check for new group
    if (line.find("BEGIN_GROUP") != std::string::npos) {
      size_t eqpos = line.find("=");
      std::string name = line.substr(eqpos+2);
      channel_index = -1;
      if (name == "BAND_C" ) channel_index = 0;
      if (name == "BAND_B" ) channel_index = 1;
      if (name == "BAND_G" ) channel_index = 2;
      if (name == "BAND_Y" ) channel_index = 3;
      if (name == "BAND_R" ) channel_index = 4;
      if (name == "BAND_RE") channel_index = 5;
      if (name == "BAND_N" ) channel_index = 6;
      if (name == "BAND_N2") channel_index = 7;
      continue;
    }
    
    // Check for values
    if (line.find("absCalFactor") != std::string::npos) {
      if (channel_index < 0)
        vw_throw( ArgumentErr() << "Error reading absCalFactor in metadata file!\n");
      metadata.abs_cal_factor[channel_index] = parse_metadata_line(line);
      ++found_count;
      continue;
    }
    if (line.find("effectiveBandwidth") != std::string::npos) {
      if (channel_index < 0)
        vw_throw( ArgumentErr() << "Error reading effectiveBandwidth in metadata file!\n");
      metadata.effective_bandwidth[channel_index] = parse_metadata_line(line);
      ++found_count;
      continue;
    } 
    if (line.find("meanSunEl") != std::string::npos) {
      metadata.mean_sun_elevation = parse_metadata_line(line);
      ++found_count;
      continue;
    }
    if (line.find("firstLineTime") != std::string::npos) {
      size_t eqpos = line.find("=");
      metadata.datetime = line.substr(eqpos+1);
      ++found_count;
      continue;
    }
  }
  handle.close();

  // Check that we got what we need
  if (found_count != 2*NUM_WORLDVIEW_BANDS+2)
    vw_throw( ArgumentErr() << "Failed to find all required metadata!\n");

  // Compute derived metadata values
  metadata.populate_derived_values();
  
} // End function read_worldview3_metadata



/// Convert an input WorldView pixel to top-of-atmosphere.
WorldView3ToaPixelType convert_to_toa(WorldView3PixelType const& pixel_in,
                                      WorldViewMetadataContainer const& metadata)
{             
  //std::cout << "IN " << pixel_in << std::endl;
  // First convert to radiance values
  WorldView3ToaPixelType rad_pixel = pixel_cast<WorldView3ToaPixelType>(pixel_in);
  for (int i=0; i<NUM_WORLDVIEW_BANDS; ++i)
    rad_pixel[i] = rad_pixel[i]*(metadata.abs_cal_factor[i]/metadata.effective_bandwidth[i]);

  // Now convert to reflectance values
  float scale_factor = metadata.earth_sun_distance*metadata.earth_sun_distance*M_PI /
                       cos(DEG_TO_RAD*(90.0 - metadata.mean_sun_elevation));
  WorldView3ToaPixelType pixel = rad_pixel;
  for (int i=0; i<NUM_WORLDVIEW_BANDS; ++i)
    pixel[i] = rad_pixel[i] * scale_factor / WORLDVIEW_ESUN[i];

  //std::cout << "OUT " << pixel << std::endl;
  return pixel;
}

/// Functor wrapper for TOA conversion function
class WorldView3ToaFunctor : public ReturnFixedType<WorldView3ToaPixelType > {
  WorldViewMetadataContainer m_metadata;
public:
  WorldView3ToaFunctor(WorldViewMetadataContainer const& metadata)
   : m_metadata(metadata) {}
  
  WorldView3ToaPixelType operator()( WorldView3PixelType const& pixel) const {
    return convert_to_toa(pixel, m_metadata);
  }
};


//TODO: Come up with detection algorithms for these two sensors!
//TODO: Verify TOA correction works!


/// Compute NDVI index
float compute_ndvi( WorldView3ToaPixelType const& pixel) {
  float denom = pixel[RED] + pixel[NIR2];
  if (denom == 0)
    return 0; // Avoid divide-by-zero
  return (pixel[RED] - pixel[NIR2]) / denom;
}

/// Compute NDWI index
float compute_ndwi( WorldView3ToaPixelType const& pixel) {
  float denom = pixel[BLUE] + pixel[NIR1];
  if (denom == 0)
    return 0; // Avoid divide-by-zero
  return (pixel[BLUE] - pixel[NIR1]) / denom;
}

/// Compute NDWI2 index
/// - Both of these calculations are sometimes listed as "NDWI"
float compute_ndwi2( WorldView3ToaPixelType const& pixel) {
  float denom = pixel[COASTAL] + pixel[NIR2];
  if (denom == 0)
    return 0; // Avoid divide-by-zero
  return (pixel[COASTAL] - pixel[NIR2]) / denom;
}

/// Use this to call detect_water on each pixel like this:
/// --> = per_pixel_view(landsat_image, landsat::DetectWaterLandsatFunctor());
class DetectWaterWorldView3Functor  : public ReturnFixedType<uint8> {
public:
  DetectWaterWorldView3Functor() {}
  
  uint8 operator()( WorldView3ToaPixelType const& pixel) const {
    if (is_valid(pixel)){
      // Extremely simple way to look for water!
      // TODO: It does not work well!  Need to test a better method on more images.
      float ndvi = compute_ndvi(pixel);
      float ndwi = compute_ndwi(pixel);
      //std::cout << "ndvi " << ndvi << ", ndwi " << ndwi <<std::endl;
      //if ((ndwi > 0.0) && (ndvi < 0.0))
      if (ndwi > 0.1)
        return FLOOD_DETECT_WATER;
      return FLOOD_DETECT_LAND;
    }
    else
      return FLOOD_DETECT_NODATA;
  }
};

/*
void detect_water_spot67(std::vector<std::string> const& image_files, 
                         std::string const& output_path,
                         cartography::GdalWriteOptions const& write_options) {

  Spot67Image spot_image;
  cartography::GeoReference georef;
  load_spot67_image(image_files, spot_image, georef);
 
  WorldViewMetadataContainer metadata;
  load_spot67_metadata(image_files, metadata);

  block_write_gdal_image(output_path,
                         apply_mask(
                           per_pixel_view(
                              per_pixel_view(
                                //crop(spot_image, BBox2(110, 2533, 1182, 1005)), // DEBUG
                                spot_image,
                                Spot67ToaFunctor(metadata)
                              ),
                              DetectWaterSpot67Functor()
                           ),
                           FLOOD_DETECT_NODATA
                         ),
                         true, georef,
                         true, nodata_out,
                         write_options,
                         TerminalProgressCallback("vw", "\t--> Classifying Spot:"));
}
*/
void detect_water_worldview3(std::vector<std::string> const& image_files, 
                             std::string const& output_path,
                             cartography::GdalWriteOptions const& write_options,
                             bool debug = false) {

  WorldView3Image wv_image;
  cartography::GeoReference georef;
  load_worldview3_image(image_files, wv_image, georef);
 
  WorldViewMetadataContainer metadata;
  load_worldview3_metadata(image_files, metadata);

  if (debug) {
    std::cout << "Loaded metadata: \n";
    std::cout << "abs_cal_factor     "  << metadata.abs_cal_factor      << std::endl;
    std::cout << "effective_bandwidth " << metadata.effective_bandwidth << std::endl;
    std::cout << "mean_sun_elevation "  << metadata.mean_sun_elevation  << std::endl;
    std::cout << "earth_sun_distance "  << metadata.earth_sun_distance  << std::endl;
    std::cout << "datetime "            << metadata.datetime            << std::endl;
  }

  block_write_gdal_image(output_path,
                         apply_mask(
                           per_pixel_view(
                              per_pixel_view(
                                //crop(wv_image, BBox2(110, 2533, 1182, 1005)), // DEBUG
                                //crop(wv_image, BBox2(896, 2905, 5, 5)), // DEBUG
                                wv_image,
                                WorldView3ToaFunctor(metadata)
                              ),
                              DetectWaterWorldView3Functor()
                           ),
                           FLOOD_DETECT_NODATA
                         ),
                         true, georef,
                         true, FLOOD_DETECT_NODATA,
                         write_options,
                         TerminalProgressCallback("vw", "\t--> Classifying WorldView:"));
}



}} // end namespace vw::multispectral

#endif
