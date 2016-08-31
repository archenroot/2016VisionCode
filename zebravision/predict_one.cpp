#include "CaffeClassifier.hpp"
#include "classifierio.hpp"

using namespace std;
using namespace cv;

int main(int argc, char **argv)
{
	::google::InitGoogleLogging(argv[0]);
	if (argc < 2)
	{
		cout << "Usage : " << argv[0] << " filelist_of_imgs.txt" << endl;
		return 1;
	}
	ClassifierIO clio("d24", 21, -1);
	vector<string> files = clio.getClassifierFiles();
	for (auto it = files.cbegin(); it != files.cend(); ++it)
		cout << *it << endl;

	ifstream infile(argv[1]);

	CaffeClassifier<Mat> c(files[0], files[1], files[2], files[3], 256); 

	Mat img;
	Mat rsz;
	Mat f32;
	vector<Mat> imgs;
	string filename;
	while(getline(infile, filename))
	{
		cout << "Read " << filename << endl;
		img = imread(filename);
		resize(img, rsz, c.getInputGeometry()); 
		rsz.convertTo(f32, CV_32FC3);
		imgs.push_back(f32.clone());
	}

	while (clio.findNextClassifierStage(false))
	{
		vector<string> files = clio.getClassifierFiles();
		CaffeClassifier<Mat> c(files[0], files[1], files[2], files[3], 256); 
		vector<vector<Prediction>> p = c.ClassifyBatch(imgs,2);
		for (auto v = p.cbegin(); v != p.cend(); ++v)
		{
			for (auto it = v->cbegin(); it != v->cend(); ++it)
				cout << it->first << " " << it->second << " ";
			cout << endl;
		}
		cout <<"---------------------"<< endl;
	}


	return 0;

}
