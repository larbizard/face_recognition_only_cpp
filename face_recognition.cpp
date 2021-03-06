/*
 *  Copyright (C) 2015-2018 Savoir-faire Linux Inc.
 *  
 *  Author: Timothée Menais <timothee.menais@savoirfairelinux.com>
 *  
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *  
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA.
 */
#include <opencv2/objdetect.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/face.hpp>

#include <iostream>
#include <fstream>


#include "tensorflow/cc/ops/const_op.h"
#include "tensorflow/cc/ops/image_ops.h"
#include "tensorflow/cc/ops/standard_ops.h"
#include "tensorflow/cc/client/client_session.h"

#include "tensorflow/core/framework/graph.pb.h"
#include "tensorflow/core/framework/tensor.h"
#include "tensorflow/core/graph/default_device.h"
#include "tensorflow/core/graph/graph_def_builder.h"
#include "tensorflow/core/lib/core/errors.h"
#include "tensorflow/core/lib/core/stringpiece.h"
#include "tensorflow/core/lib/core/threadpool.h"
#include "tensorflow/core/lib/io/path.h"
#include "tensorflow/core/lib/strings/str_util.h"
#include "tensorflow/core/lib/strings/stringprintf.h"
#include "tensorflow/core/platform/env.h"
#include "tensorflow/core/platform/init_main.h"
#include "tensorflow/core/platform/logging.h"
#include "tensorflow/core/platform/types.h"
#include "tensorflow/core/public/session.h"
#include "tensorflow/core/util/command_line_flags.h"
 
using namespace std;
using namespace cv;
using namespace tensorflow;
using namespace cv::face;



static tensorflow::Status ReadEntireFile(tensorflow::Env* env, const std::string& filename,
                             Tensor* output) {
    tensorflow::uint64 file_size = 0;
    TF_RETURN_IF_ERROR(env->GetFileSize(filename, &file_size));

    std::string contents;
    contents.resize(file_size);

    std::unique_ptr<tensorflow::RandomAccessFile> file;
    TF_RETURN_IF_ERROR(env->NewRandomAccessFile(filename, &file));

    tensorflow::StringPiece data;
    TF_RETURN_IF_ERROR(file->Read(0, file_size, &data, &(contents)[0]));
    if (data.size() != file_size) {
    return tensorflow::errors::DataLoss("Truncated read of '", filename,
                                        "' expected ", file_size, " got ",
                                        data.size());
    }
    output->scalar<std::string>()() = data.ToString();
    return Status::OK();
}


tensorflow::Status LoadGraph(const std::string& graph_file_name,
                 std::unique_ptr<tensorflow::Session>* session) {
    tensorflow::GraphDef graph_def;
    Status load_graph_status =
        ReadBinaryProto(tensorflow::Env::Default(), graph_file_name, &graph_def);
            if (!load_graph_status.ok()) {
            return tensorflow::errors::NotFound("Failed to load compute graph at '",
                                        graph_file_name, "'");
    }
    
    session->reset(tensorflow::NewSession(tensorflow::SessionOptions()));
    Status session_create_status = (*session)->Create(graph_def);
    if (!session_create_status.ok()) {
        return session_create_status;
    }

    return Status::OK();
}

std::unique_ptr<tensorflow::Session> initSession(std::string graphpath){
    std::unique_ptr<tensorflow::Session> session;
    Status load_graph_status = LoadGraph(graphpath, &session);
    
    if (!load_graph_status.ok()) {
    }   
  
    return session;
}

std::vector<std::string> ReadLabelsAndEmbeddings(const std::string file_name) {
    
    std::ifstream infile(file_name);
    std::string label_file;
    std::vector<std::string> labels_files;
    if(infile.is_open()){
        while(std::getline(infile, label_file)){
            labels_files.push_back(label_file);
        }
          infile.close();
    }
    return labels_files;
}

std::vector<float> ConvStringToFloats(std::string str){

    std::vector<float> vect;
    vect.reserve(512); // embedding is 512 component vector
    std::istringstream stm(str) ;
    
    float number;

    while(stm >> number){
        vect.push_back(number);
    }

    return vect;
}

bool isFloat(string s) {
    istringstream iss(s);
    float dummy;
    iss >> noskipws >> dummy;
    return iss && iss.eof();     // Result converted to bool
}


cv::Mat faceCenterRotateCrop(Mat &im, vector<Point2f> landmarks, Rect face, int i, bool show_crop){

    //description of the landmarks in case someone wants to do something custom
    // landmarks 0, 16           // Jaw line
    // landmarks 17, 21          // Left eyebrow
    // landmarks 22, 26          // Right eyebrow
    // landmarks 27, 30          // Nose bridge
    // landmarks 30, 35          // Lower nose
    // landmarks 36, 41          // Left eye
    // landmarks 42, 47          // Right Eye
    // landmarks 48, 59          // Outer lip
    // landmarks 60, 67          // Inner lip


    // 2D image points. If you change the image, you need to change vector
    std::vector<cv::Point2d> image_points;
    image_points.push_back(landmarks[30]);    // Nose tip
    image_points.push_back(landmarks[8]);     // Chin
    image_points.push_back(landmarks[45]);    // Left eye left corner
    image_points.push_back(landmarks[36]);    // Right eye right corner
    image_points.push_back(landmarks[54]);    // Left Mouth corner
    image_points.push_back(landmarks[48]);    // Right mouth corner

    // 3D model points.
    std::vector<cv::Point3d> model_points;
    model_points.push_back(cv::Point3d(0.0f, 0.0f, 0.0f));               // Nose tip
    model_points.push_back(cv::Point3d(0.0f, -330.0f, -65.0f));          // Chin
    model_points.push_back(cv::Point3d(-225.0f, 170.0f, -135.0f));       // Left eye left corner
    model_points.push_back(cv::Point3d(225.0f, 170.0f, -135.0f));        // Right eye right corner
    model_points.push_back(cv::Point3d(-150.0f, -150.0f, -125.0f));      // Left Mouth corner
    model_points.push_back(cv::Point3d(150.0f, -150.0f, -125.0f));       // Right mouth corner

    // Camera internals
    double focal_length = im.cols; // Approximate focal length. //3 nb channels
    Point2d center = cv::Point2d(im.cols/2,im.rows/2);
    cv::Mat camera_matrix = (cv::Mat_<double>(3,3) << focal_length, 0, center.x, 0 , focal_length, center.y, 0, 0, 1);
    cv::Mat dist_coeffs = cv::Mat::zeros(4,1,cv::DataType<double>::type); // Assuming no lens distortion

    cv::Mat rotation_vector; // Rotation in axis-angle form
    cv::Mat translation_vector;
         
    // Solve for pose
    cv::solvePnP(model_points, image_points, camera_matrix, dist_coeffs, rotation_vector, translation_vector);

    // Access the last element in the Rotation Vector
    double rot = rotation_vector.at<double>(0,2);
    double theta_deg = rot/M_PI*180;
    
    // Rotate around the center    
    Point2f pt = landmarks[30]; //center is nose tip
    Mat r = getRotationMatrix2D(pt, theta_deg, 1.0);
    // determine bounding rectangle
    cv::Rect bbox = cv::RotatedRect(pt,im.size(), theta_deg).boundingRect();

    // Apply affine transform
    Mat dst;
    warpAffine(im, dst, r, bbox.size());

    // Now crop the face
    Mat Cropped_Face = dst(face);

    resize( Cropped_Face, Cropped_Face, cv::Size(160, 160), CV_INTER_LINEAR);

    

    if (show_crop == true)
    {            
        std::string text = "Cropped Face ";
        text += std::to_string(i);
        imshow(text,Cropped_Face);
    }

    return Cropped_Face ;
}

 
// Function for Face Detection
void detectAndDraw( Mat& img, CascadeClassifier& cascade,
                    Ptr<Facemark> facemark,
                    double scale, std::unique_ptr<tensorflow::Session>* session,
                    std::vector<std::string> label_database,
                    std::vector<std::string> embeddings_database,
                    bool show_crop, float thresh)
{
    vector<Rect> faces;
    Mat smallImg;
    double fx = 1 / scale; 
    resize( img, smallImg, Size(), fx, fx, CV_INTER_LINEAR ); 
   
    cascade.detectMultiScale( smallImg, faces, 1.1, 
                            7, 0|CASCADE_SCALE_IMAGE, Size(30, 30) );

    
    int linewidth = std::max(1, int(img.rows * .005));

    vector<vector<Point2f>> landmarks;

    bool success = facemark->fit(smallImg,faces,landmarks);
 
    // Draw boxes around the faces
    for ( size_t i = 0; i < faces.size(); i++ )
    {
        Rect r = faces[i];
        
        Scalar color = Scalar(255, 0, 0); // Color for Drawing tool

        rectangle( img, cvPoint(cvRound(r.x*scale), cvRound(r.y*scale)),
                cvPoint(cvRound((r.x + r.width-1)*scale), 
                cvRound((r.y + r.height-1)*scale)), color, linewidth, 8, 0);

        Mat smallImgROI ;

        if(success)
        {
        // If success, align face
            if (landmarks[i].size()==68)
            {
                 smallImgROI = faceCenterRotateCrop(smallImg,landmarks[i],faces[i],i,show_crop);
            }

        }

        if(! smallImgROI.data )                // Check for invalid input
        {
            cout <<  "Could not open or find the image" << std::endl ;
            return ;
        }

          
        auto data = smallImgROI.data;
        Tensor input_tensor(tensorflow::DT_FLOAT, tensorflow::TensorShape({1,smallImgROI.rows,smallImgROI.cols,3}));
        auto input_tensor_mapped = input_tensor.tensor<float, 4>(); 

        for (int x = 0; x < smallImgROI.cols; ++x) {
            for (int y = 0; y < smallImgROI.rows; ++y) {
                for (int c = 0; c < 3; ++c) {
                    int offset = y * smallImgROI.cols + x * 3 + c;
                    input_tensor_mapped(0, y, x, c) = tensorflow::uint8(data[offset]);
                }
            }
        }

        //Tensor Flow graph specifics :
        string input_layer = "input";
        string phase_train_layer = "phase_train";
        string output_layer = "embeddings";
        tensorflow::Tensor phase_tensor(tensorflow::DT_BOOL, tensorflow::TensorShape());
        phase_tensor.scalar<bool>()() = false;

        //Run session
        std::vector<Tensor> outputs ;
        Status run_status = session->get()->Run({{input_layer, input_tensor},
                                       {phase_train_layer, phase_tensor}}, 
                                       {output_layer}, {}, &outputs);

        if (!run_status.ok()) {
            LOG(ERROR) << "\tRunning model failed: " << run_status << "\n";
            return ;
        }

        auto output_c = outputs[0].tensor<float, 2>();

        float min_emb_diff =10000;
        
        int posofmin=0;


        for (int i = 0; i < label_database.size(); ++i){
            float diff=0;

            std::vector<float> vect = ConvStringToFloats(embeddings_database[i]);


            for (int j = 0; j < outputs[0].shape().dim_size(1); ++j){
                diff += (output_c(0,j) - vect[j]) * (output_c(0,j) - vect[j]) ;
            }
            //diff= diff; //no need to sqrt
            if (diff < min_emb_diff)
            {
                min_emb_diff = diff ;
                posofmin = i ;
            }                
        }

        cv::Point txt_up = cvPoint(cvRound(r.x*scale + linewidth ), cvRound(r.y*scale - 4 * linewidth));      
        cv::Point txt_in = cvPoint(cvRound(r.x*scale + linewidth ), cvRound(r.y*scale + 12 * linewidth));

        if(min_emb_diff < thresh) {
            cout <<"Hello " << label_database[posofmin] << " confidence: " << min_emb_diff << endl;
            if ( cvRound(r.y*scale -12 * linewidth) > 0 )
            {
                cv::putText(img, label_database[posofmin], txt_up, 1, linewidth , color );
            }else{ // write in box if box is too high
                cv::putText(img, label_database[posofmin], txt_in, 1, linewidth , color );
            }
        }else{
            cout <<"WHO ARE YOU ?"<< " confidence: " << min_emb_diff << endl;
            if ( cvRound(r.y*scale - 12 * linewidth) > 0 )
            {
                cv::putText(img, "404", txt_up, 1, linewidth , color );
            }else{
                cv::putText(img, "404", txt_in, 1, linewidth , color );
            }
        }

    }
 
    // Show Processed Image with detected faces
    imshow( "Face Detection", img );
}


int main( int argc, const char** argv )
{

    if( argc < 2)
    {
        cout <<" Provide your embeddings database" << endl;
        return -1;
    }

    bool show_crop = false;
    float thresh = 0.045;

    if (argc > 2)
    {
        
        for (int i = 0; i < argc; ++i)
        {
           
           if (strcmp( argv[i], "-show_crop") == 0)
           {
               show_crop = true ;
           }

           if (strcmp( argv[i], "-thresh") == 0 )
           {
               if (i != argc-1 && isFloat(argv[i+1]))
               {
                   thresh = atof(argv[i+1]);
                   
               } else {
                cout <<" No threshold given, keeps " << thresh << " default " << endl;
               }
               
           }

        }
    }

    std::vector<std::string> database = ReadLabelsAndEmbeddings(argv[1]);
    std::vector<std::string> label_database;
    std::vector<std::string> embeddings_database;
    label_database.reserve(database.size());
    embeddings_database.reserve(database.size());
    std::vector<std::vector<float>> embeddings_float;
    embeddings_float.reserve(database.size());


    for (int i = 0; i < database.size(); ++i){

            label_database.push_back( database[i].substr(0,  database[i].find_first_of(" ")));
            embeddings_database.push_back( database[i].substr( database[i].find_first_of(" ")+1));

    } 

    // VideoCapture class for playing video for which faces to be detected
    VideoCapture capture; 
    Mat frame;
  
    // Load everything needed    
    Ptr<Facemark> facemark = FacemarkLBF::create();
    facemark->loadModel("lbfmodel.yaml");

    CascadeClassifier cascade;
    double scale=1; 
    cascade.load("haarcascade_frontalface_alt2.xml") ;

    std::unique_ptr<tensorflow::Session> session = initSession("20170512-110547.pb");

 
    //Start Video..1) 0 for WebCam 2) "Path to Video" for a Local Video
    capture.open(0); 
    if( capture.isOpened() )
    {
        // Capture frames from video and detect faces
        cout << "Face Detection Started...." << endl;
        while(1)
        {
            capture >> frame;
            if( frame.empty() )
                break;
            Mat frame1 = frame.clone();
            detectAndDraw(frame1, cascade,
                           facemark,
                           scale,
                           &session,
                           label_database,
                           embeddings_database,
                           show_crop,
                           thresh
                           );
            char c = (char)waitKey(10);
         
            // Press q to exit from window
            if( c == 27 || c == 'q' || c == 'Q' ) 
                break;
        }
    }
    else
        cout<<"Could not Open Camera";
    return 0;
}
 
