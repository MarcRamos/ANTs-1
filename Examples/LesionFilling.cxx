#include <iostream>
#include <ostream>
#include <sstream>
#include <iostream>
#include <vector>

#include "antsUtilities.h"

#include "itkImage.h"
#include "itkImageFileReader.h"
#include "itkImageFileWriter.h"
#include "itkCastImageFilter.h"
#include "itkSubtractImageFilter.h"
#include "itkBinaryDilateImageFilter.h"
#include "itkBinaryBallStructuringElement.h"
#include "itkMaskImageFilter.h"
#include "itkConnectedComponentImageFilter.h"
#include "itkBinaryThresholdImageFilter.h"
#include "itkImageRegionIterator.h"
//LesionFilling dimension t1.nii.gz lesionmask output.nii.gz
namespace ants
{
template <unsigned int ImageDimension>
int LesionFilling( int argc, char * argv[] )
{
  typedef unsigned char PixelType;
  typedef itk::Image<PixelType, ImageDimension> ImageType;
  typedef itk::ImageFileReader<ImageType>  ImageReaderType;
  typedef itk::ImageRegionIterator< ImageType> IteratorType;
  const char * T1FileName = argv[2];
  const char * LesionMapFileName = argv[3];
  const char * OutputFileName = argv[4];

  typename ImageReaderType::Pointer LesionReader = ImageReaderType::New();
  LesionReader->SetFileName( LesionMapFileName );
  LesionReader->Update();

  typename ImageReaderType::Pointer T1Reader = ImageReaderType::New();
  T1Reader->SetFileName( T1FileName); 
  T1Reader->Update();
  
  typedef double           RealPixelType;  //  Operations
  typedef itk::Image<RealPixelType, ImageDimension> RealImageType;
  typedef itk::CastImageFilter< ImageType, RealImageType> CastToRealFilterType;
  typename CastToRealFilterType::Pointer LesiontoReal = CastToRealFilterType::New();
  LesiontoReal->SetInput( LesionReader->GetOutput() );
  typename CastToRealFilterType::Pointer T1toReal = CastToRealFilterType::New();
  T1toReal->SetInput( T1Reader->GetOutput() );

  typedef itk::BinaryThresholdImageFilter <ImageType, ImageType>
                             BinaryThresholdImageFilterType;
  typedef itk::BinaryBallStructuringElement<
                              RealPixelType,
                              ImageDimension> StructuringElementType;
  typedef itk::BinaryDilateImageFilter<
                               ImageType,
                               ImageType,
                               StructuringElementType >  DilateFilterType;
  typedef itk::SubtractImageFilter <ImageType, ImageType>
                               SubtractImageFilterType;
  //finding connected components, we assume each component is one lesion
  typedef itk::ConnectedComponentImageFilter <ImageType, ImageType>
              ConnectedComponentImageFilterType;
  typename ConnectedComponentImageFilterType::Pointer connected =
              ConnectedComponentImageFilterType::New ();
  connected->SetInput( LesiontoReal->GetOutput() ) ;
  connected->Update();
  const int LesionNumber = connected->GetObjectCount() ;
  std::cout << "Number of lesions: " << LesionNumber << std::endl;
  for ( int i = 1; i < LesionNumber; i++)
  {
     std::vector<double> outervoxels;
     typename BinaryThresholdImageFilterType::Pointer thresholdFilter
                = BinaryThresholdImageFilterType::New();
     thresholdFilter->SetInput(connected->GetOutput());
     thresholdFilter->SetLowerThreshold( (double) i - 0.1);
     thresholdFilter->SetUpperThreshold( (double) i + 0.1);
     thresholdFilter->SetInsideValue  ( 1 );
     thresholdFilter->SetOutsideValue ( 0 );
     //Neighbouring voxel
     //filling lesions with the voxels surrounding them
     //first finding the edges of lesions
     //by subtracting dilated lesion map from lesion map itself
     typename DilateFilterType::Pointer binaryDilate = DilateFilterType::New();

     StructuringElementType structuringElement;
     structuringElement.SetRadius( 1 );  // 3x3 structuring element
     structuringElement.CreateStructuringElement();
     binaryDilate->SetKernel( structuringElement );
     binaryDilate->SetInput( thresholdFilter->GetOutput() );
     binaryDilate->SetDilateValue( 1 );
     // subtract dilated image form non-dilated one
     typename SubtractImageFilterType::Pointer subtractFilter
                   = SubtractImageFilterType::New ();
     //output = image1 - image2
     subtractFilter->SetInput1( binaryDilate->GetOutput() );
     subtractFilter->SetInput3( thresholdFilter->GetOutput() );
     subtractFilter->Update();
     //multiply the outer lesion mask with T1 to get only the neighbouring voxels
     typedef itk::MaskImageFilter< ImageType, ImageType > MaskFilterType;
     typename MaskFilterType::Pointer maskFilter = MaskFilterType::New();
     maskFilter->SetInput(T1toReal->GetOutput() );
     maskFilter->SetMaskImage( subtractFilter->GetOutput() );
     //collecting non-zero voxels
     IteratorType it( maskFilter->GetOutput(),
                       maskFilter->GetOutput()->GetLargestPossibleRegion() );
     it.GoToBegin();
     /** Walk over the image. */
     while ( !it.IsAtEnd() )
       {
         if( it.Value() )
         {
           outervoxels.push_back ( it.Get() ); 
         }
         ++it;
       } // end while
     //calculating mean lesion intesity
     //Note: lesions should not be filled with values
     //less than their originial values, this is a
     //trick to exclude any CSF voxels in the outer mask (if any)
     typename MaskFilterType::Pointer maskFilterLesion = MaskFilterType::New();
     maskFilterLesion->SetInput( T1toReal->GetOutput() );
     maskFilterLesion->SetMaskImage( LesiontoReal->GetOutput() );
     IteratorType it2( maskFilterLesion->GetOutput(),
                       maskFilterLesion->GetOutput()->GetLargestPossibleRegion() );
     it2.GoToBegin();
     /** Walk over the image. */
     int counter  = 0;
     double meanInsideLesion = 0;
     while ( !it2.IsAtEnd() )
       {
         if( it2.Value() )
         {
           //coutning number of voxels inside lesion
           counter++;
           meanInsideLesion += it2.Get();
         }
         ++it2;
       }
     meanInsideLesion /= counter;
     //check that all outer voxels are more than the mean 
     //intensity of the lesion, i.e. not including CSF voxels
     
     IteratorType it3( maskFilter->GetOutput(),
                       maskFilter->GetOutput()->GetLargestPossibleRegion() );
     it3.GoToBegin();
     std::vector<double> outerWMVoxels;
     while ( !it3.IsAtEnd() )
     {
       if ( it3.Get() > meanInsideLesion )
       {
         outerWMVoxels.push_back( it3.Get() );
       }//end if
       ++it3;
    }//end while
    //walk through original T1
    //and chagne inside the lesion with a random pick from
    //collected normal appearing WM voxels (outerWMVoxels)
    IteratorType it4( T1Reader->GetOutput(),
                     T1Reader->GetOutput()->GetLargestPossibleRegion() );
    IteratorType itL(thresholdFilter->GetOutput(),
                     thresholdFilter->GetOutput()->GetLargestPossibleRegion() );
    int max = outerWMVoxels.size();
    int min = 0;
    int index = min + (rand() % (int)(max - min + 1)) ;
    it4.GoToBegin();
    itL.GoToBegin();
    while ( !it4.IsAtEnd() )
    {
      if (itL.Get() == 1)
      {
        it4.Set( outerWMVoxels[ index ] );
      }
    }
  }//loop for lesions
  return 0;
}//main int
}//namespace ants
