#include "mainwindow.h"
#include "ui_mainwindow.h"

using namespace std;
using namespace cv;

int v1 = 50;
int v2 = 30;
int SMOOTHING_RADIUS ; // In frames. The larger the more stable the video, but less reactive to sudden panning
int HORIZONTAL_BORDER_CROP ; // In pixels. Crops the border to reduce the black borders from stabilisation being too noticeable.
QString fileName;

struct TransformParam
{
    TransformParam() {}
    TransformParam(double _dx, double _dy, double _da) {
        dx = _dx;
        dy = _dy;
        da = _da;
    }

    double dx;
    double dy;
    double da; // angle
};

struct Trajectory
{
    Trajectory() {}
    Trajectory(double _x, double _y, double _a) {
        x = _x;
        y = _y;
        a = _a;
    }

    double x;
    double y;
    double a; // angle
};

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
}

MainWindow::~MainWindow()
{
    delete ui;
}


void MainWindow::on_actionopen_triggered()
{
    fileName = QFileDialog::getOpenFileName(this,tr("Open Image"), ".");

    //    cv::VideoCapture capVideo;

    //    cv::Mat imgFrame;

    //    capVideo.open(fileName.toStdString());

    //    if (!capVideo.isOpened()) {                                                 // if unable to open video file
    //        std::cout << "\nerror reading video file" << std::endl << std::endl;      // show error message
    //        _getch();                    // it may be necessary to change or remove this line if not using Windows
    //    }
    //    if (capVideo.get(CV_CAP_PROP_FRAME_COUNT) < 1) {
    //        std::cout << "\nerror: video file must have at least one frame";
    //        _getch();
    //    }

    //    capVideo.read(imgFrame);

    //    char chCheckForEscKey = 0;

    //    while (capVideo.isOpened() && chCheckForEscKey != 27) {

    //        cv::imshow("imgFrame", imgFrame);

    //                // now we prepare for the next iteration

    //        if ((capVideo.get(CV_CAP_PROP_POS_FRAMES) + 1) < capVideo.get(CV_CAP_PROP_FRAME_COUNT)) {       // if there is at least one more frame
    //            capVideo.read(imgFrame);                            // read it
    //        }
    //        else {                                                  // else
    //            std::cout << "end of video\n";                      // show end of video message
    //            break;                                              // and jump out of while loop
    //        }

    //        chCheckForEscKey = cv::waitKey(1);      // get key press in case user pressed esc
    //    }


    //---------------------------------------------------------------------------------------

    //    if(argc < 2) {
    //           cout << "./VideoStab [video.avi]" << endl;
    //           return 0;
    //       }

    // For further analysis


}

void MainWindow::on_spinBox_valueChanged(int arg1)
{
    v1 = arg1;
}

void MainWindow::on_spinBox_2_valueChanged(int arg1)
{
    v2 = arg1;
}

void MainWindow::on_pushButton_clicked()
{
    SMOOTHING_RADIUS = v1;
    HORIZONTAL_BORDER_CROP = v2;
    ofstream out_transform("prev_to_cur_transformation.txt");
    ofstream out_trajectory("trajectory.txt");
    ofstream out_smoothed_trajectory("smoothed_trajectory.txt");
    ofstream out_new_transform("new_prev_to_cur_transformation.txt");

    VideoCapture cap(fileName.toStdString());  //Input the video
    Mat cur, cur_grey;
    Mat prev, prev_grey;

    cap >> prev;
    cvtColor(prev, prev_grey, COLOR_BGR2GRAY);

    // Step 1 - Get previous to current frame transformation (dx, dy, da) for all frames
    vector <TransformParam> prev_to_cur_transform; // previous to current

    int k=1;
    int max_frames = cap.get(CV_CAP_PROP_FRAME_COUNT);
    Mat last_T;

    while(true) {
        cap >> cur;

        if(cur.data == NULL) {
            break;
        }

        cvtColor(cur, cur_grey, COLOR_BGR2GRAY);

        // Vector from previous to current frame
        vector <Point2f> prev_corner, cur_corner;
        vector <Point2f> prev_corner2, cur_corner2;
        vector <uchar> status;
        vector <float> err;

        goodFeaturesToTrack(prev_grey, prev_corner, 200, 0.01, 30);
        calcOpticalFlowPyrLK(prev_grey, cur_grey, prev_corner, cur_corner, status, err);

        // Delete bad matches
        for(size_t i=0; i < status.size(); i++) {
            if(status[i]) {
                prev_corner2.push_back(prev_corner[i]);
                cur_corner2.push_back(cur_corner[i]);
            }
        }

        // Translation + rotation only
        Mat T = estimateRigidTransform(prev_corner2, cur_corner2, false); // false = rigid transform, no scaling/shearing

        // In rare cases no transform is found. We'll just use the last known good transform.
        if(T.data == NULL) {
            last_T.copyTo(T);
        }

        T.copyTo(last_T);

        // Decompose Transformation to dx, dy, da
        double dx = T.at<double>(0,2);
        double dy = T.at<double>(1,2);
        double da = atan2(T.at<double>(1,0), T.at<double>(0,0));

        prev_to_cur_transform.push_back(TransformParam(dx, dy, da)); // Put them into vectors

        out_transform << k << " " << dx << " " << dy << " " << da << endl; //analysis

        cur.copyTo(prev);
        cur_grey.copyTo(prev_grey);

        cout << "Frame: " << k << "/" << max_frames << " - good optical flow: " << prev_corner2.size() << endl;
        k++;
    }

    // Step 2 - Accumulate the transformations to get the image trajectory

    // Accumulated frame to frame transform
    double a = 0;
    double x = 0;
    double y = 0;

    vector <Trajectory> trajectory; // trajectory at all frames

    for(size_t i=0; i < prev_to_cur_transform.size(); i++) {
        x += prev_to_cur_transform[i].dx;
        y += prev_to_cur_transform[i].dy;
        a += prev_to_cur_transform[i].da;

        trajectory.push_back(Trajectory(x,y,a));

        out_trajectory << (i+1) << " " << x << " " << y << " " << a << endl; //analysis
    }

    // Smooth out the trajectory using an sliding average window
    vector <Trajectory> smoothed_trajectory; // trajectory at all frames

    for(size_t i=0; i < trajectory.size(); i++) {
        double sum_x = 0;
        double sum_y = 0;
        double sum_a = 0;
        int count = 0;

        for(int j=-SMOOTHING_RADIUS; j <= SMOOTHING_RADIUS; j++) {
            if(i+j >= 0 && i+j < trajectory.size()) {
                sum_x += trajectory[i+j].x;
                sum_y += trajectory[i+j].y;
                sum_a += trajectory[i+j].a;

                count++;
            }
        }

        double avg_a = sum_a / count;
        double avg_x = sum_x / count;
        double avg_y = sum_y / count;

        smoothed_trajectory.push_back(Trajectory(avg_x, avg_y, avg_a));

        out_smoothed_trajectory << (i+1) << " " << avg_x << " " << avg_y << " " << avg_a << endl; //analysis
    }

    // Step 3 - Generate new set of previous to current transform, such that the trajectory ends up being the same as the smoothed trajectory
    vector <TransformParam> new_prev_to_cur_transform;

    // Accumulated frame to frame transform
    a = 0;
    x = 0;
    y = 0;

    for(size_t i=0; i < prev_to_cur_transform.size(); i++) {
        x += prev_to_cur_transform[i].dx;
        y += prev_to_cur_transform[i].dy;
        a += prev_to_cur_transform[i].da;

        // Target - current
        double diff_x = smoothed_trajectory[i].x - x;
        double diff_y = smoothed_trajectory[i].y - y;
        double diff_a = smoothed_trajectory[i].a - a;

        double dx = prev_to_cur_transform[i].dx + diff_x;
        double dy = prev_to_cur_transform[i].dy + diff_y;
        double da = prev_to_cur_transform[i].da + diff_a;

        new_prev_to_cur_transform.push_back(TransformParam(dx, dy, da));

        out_new_transform << (i+1) << " " << dx << " " << dy << " " << da << endl; //analysis
    }

    // Step 4 - Apply the new transformation to the video
    cap.set(CV_CAP_PROP_POS_FRAMES, 0);
    Mat T(2,3,CV_64F);
//    QImage img_cur(cur.rows, cur.cols, QImage::Format_RGB32);

    int vert_border = HORIZONTAL_BORDER_CROP * prev.rows / prev.cols; // get the aspect ratio correct

    k=0;
    while(k < max_frames-1) { // Don't process the very last frame, no valid transform
        cap >> cur;

        if(cur.data == NULL) {
            break;
        }

        T.at<double>(0,0) = cos(new_prev_to_cur_transform[k].da);
        T.at<double>(0,1) = -sin(new_prev_to_cur_transform[k].da);
        T.at<double>(1,0) = sin(new_prev_to_cur_transform[k].da);
        T.at<double>(1,1) = cos(new_prev_to_cur_transform[k].da);

        T.at<double>(0,2) = new_prev_to_cur_transform[k].dx;
        T.at<double>(1,2) = new_prev_to_cur_transform[k].dy;

        Mat cur2;

        warpAffine(cur, cur2, T, cur.size());

        cur2 = cur2(Range(vert_border, cur2.rows-vert_border), Range(HORIZONTAL_BORDER_CROP, cur2.cols-HORIZONTAL_BORDER_CROP));

        // Resize cur2 back to cur size, for better side by side comparison
        cv::resize(cur2, cur2, cur.size());

        // Now draw the original and stablised side by side for comparison
        Mat canvas = Mat::zeros(cur.rows, cur.cols*2+10, cur.type());

        cur.copyTo(canvas(Range::all(), Range(0, cur2.cols)));
        cur2.copyTo(canvas(Range::all(), Range(cur2.cols+10, cur2.cols*2+10)));

        // If too big to fit on the screen, then scale it down by 2
        if(canvas.cols > 1920) {
            cv::resize(canvas, canvas, Size(canvas.cols/2, canvas.rows/2));
            //            cv::resize(canvas, canvas, Size(768*2, 576*2));
        }

                imshow("before and after", canvas);

        waitKey(20);

        k++;
    }
}
