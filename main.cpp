#include <QApplication>
#include <QWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QLabel>
#include <QFileDialog>
#include <QDir>
#include <QDateTime>
#include <QCoreApplication>
#include <iostream>
#include <iomanip>
#include <opencv2/opencv.hpp>

// Include the RKNN model header from the backend
#include "rknn_model.h"

// ==========================================
// 1. THE AI MODULE (RKNN NPU Backend)
// ==========================================
class AIEngine {
private:
    rknn_model* model;
    int ctx_index;

public:
    AIEngine(const std::string& modelPath) {
        // Initialize the RKNN model
        model = new rknn_model(modelPath);
        ctx_index = 0; // Defaulting to the first NPU context
        std::cout << ">> AI Engine Initialized (RKNN NPU Mode)." << std::endl;
    }

    ~AIEngine() {
        delete model;
    }

    double processImage(const std::string& inputPath, const std::string& outputPath) {
        cv::Mat image = cv::imread(inputPath);
        if (image.empty()) return 0.0;

        // Convert BGR to RGB for RKNN
        cv::cvtColor(image, image, cv::COLOR_BGR2RGB);

        double start_time = cv::getTickCount();

        object_detect_result_list od_results;
        
        // Note: run_inference will modify 'image' (cropping/aligning) internally
        int ret = model->run_inference(image, ctx_index, &od_results);

        double end_time = cv::getTickCount();

        if (ret < 0) {
            std::cerr << "rknn_run fail! ret=" << ret << std::endl;
            return 0.0;
        }

        // Convert back to BGR for drawing and saving colors correctly
        cv::Mat image_bgr = image.clone();
        cv::cvtColor(image_bgr, image_bgr, cv::COLOR_RGB2BGR);

        // Draw bounding boxes based on RKNN output
        for (int i = 0; i < od_results.count; ++i) {
            object_detect_result result = od_results.results[i];

            cv::Rect rect(result.box.left, result.box.top, 
                          result.box.right - result.box.left, 
                          result.box.bottom - result.box.top);
            cv::rectangle(image_bgr, rect, cv::Scalar(0, 255, 0), 2); // Green box

            // Add text labels
            std::ostringstream label;
            label << "ID: " << result.cls_id << " Conf: " << std::fixed << std::setprecision(2) << result.prop;
            int baseLine = 0;
            cv::Size label_size = cv::getTextSize(label.str(), cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseLine);
            cv::rectangle(image_bgr, cv::Point(result.box.left, result.box.top - label_size.height),
                          cv::Point(result.box.left + label_size.width, result.box.top + baseLine),
                          cv::Scalar(0, 255, 0), -1);
            cv::putText(image_bgr, label.str(), cv::Point(result.box.left, result.box.top),
                        cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 0), 1, cv::LINE_AA);
        }

        cv::imwrite(outputPath, image_bgr);

        return ((end_time - start_time) / cv::getTickFrequency()) * 1000.0;
    }
};

// ==========================================
// 2. THE GUI MODULE (Separated UI)
// ==========================================
class MainWindow : public QWidget {
private:
    AIEngine* aiEngine;
    QLabel* statusLabel;
    QLabel* statsLabel;
    QPushButton* btnBulkProcess;

public:
    MainWindow(AIEngine* engine) : aiEngine(engine) {
        setWindowTitle("RKNN Bulk Processor");
        resize(500, 250);

        QVBoxLayout* layout = new QVBoxLayout(this);
        
        btnBulkProcess = new QPushButton("Select Images for Bulk Processing");
        btnBulkProcess->setStyleSheet("padding: 10px; font-weight: bold;");
        
        statusLabel = new QLabel("System Ready. Waiting for images...");
        statsLabel = new QLabel("Total Time: 0 ms | Avg Time/Image: 0 ms");
        statsLabel->setStyleSheet("color: blue; font-weight: bold;");

        layout->addWidget(btnBulkProcess);
        layout->addWidget(statusLabel);
        layout->addWidget(statsLabel);

        connect(btnBulkProcess, &QPushButton::clicked, this, &MainWindow::runBulkProcessing);
    }

private:
    void runBulkProcessing() {
        QStringList fileNames = QFileDialog::getOpenFileNames(this, "Select Images", "", "Images (*.png *.jpg *.jpeg)");
        
        if (fileNames.isEmpty()) {
            statusLabel->setText("No files selected.");
            return;
        }

        QString folderName = "results_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
        QDir().mkdir(folderName);

        int totalImages = fileNames.size();
        int processedCount = 0;
        double totalTimeMs = 0.0;

        btnBulkProcess->setEnabled(false); 

        for (const QString& qFilePath : fileNames) {
            std::string inputPath = qFilePath.toStdString();
            
            QFileInfo fileInfo(qFilePath);
            std::string outputPath = folderName.toStdString() + "/" + fileInfo.fileName().toStdString();

            processedCount++;

            if (processedCount % 10 == 0 || processedCount == totalImages) {
                statusLabel->setText(QString("Processing %1 of %2...").arg(processedCount).arg(totalImages));
                QCoreApplication::processEvents(); 
            }
            
            QCoreApplication::processEvents(); 

            double timeTaken = aiEngine->processImage(inputPath, outputPath);
            totalTimeMs += timeTaken;
        }

        double avgTime = totalTimeMs / processedCount;
        
        statusLabel->setText(QString("Finished! Saved to folder: %1").arg(folderName));
        statsLabel->setText(QString("Total Time: %1 ms | Avg Time/Image: %2 ms")
                                .arg(totalTimeMs, 0, 'f', 2)
                                .arg(avgTime, 0, 'f', 2));

        btnBulkProcess->setEnabled(true);
    }
};

// ==========================================
// 3. MAIN APPLICATION START
// ==========================================
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    // Initialize the RKNN Engine (Ensure the correct model path)
    AIEngine engine("yolo11n.rknn"); //

    MainWindow window(&engine);
    window.show();

    return app.exec();
}