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
        ctx_index = 0; // Defaulting to the first NPU core context
        std::cout << ">> AI Engine Initialized (6 TOPS Hardware NPU Active)." << std::endl;
    }

    ~AIEngine() {
        delete model;
    }

    double processImage(const std::string& inputPath, const std::string& outputPath) {
        // Load image natively (OpenCV keeps this in BGR layout)
        cv::Mat image = cv::imread(inputPath);
        if (image.empty()) return 0.0;

        // FIXED: Removed cv::cvtColor(..., COLOR_BGR2RGB).
        // The RGA hardware driver and adaptive_letterbox expect native BGR.
        
        double start_time = cv::getTickCount();

        object_detect_result_list od_results;
        
        // Execute hardware accelerated inference loop
        int ret = model->run_inference(image, ctx_index, &od_results);

        double end_time = cv::getTickCount();

        if (ret < 0) {
            std::cerr << "rknn_run fail! ret=" << ret << std::endl;
            return 0.0;
        }

        // FIXED: Removed redundant .clone() and reverse color space conversions.
        // 'image' has been efficiently updated/cropped by the backend alignment engine.
        // We draw directly on the existing matrix memory space.

        // Draw bounding boxes based on RKNN outputs
        for (int i = 0; i < od_results.count; ++i) {
            object_detect_result result = od_results.results[i];

            // Constrain bounding coordinates safely inside the image dimensions
            int x1 = std::max(0, result.box.left);
            int y1 = std::max(0, result.box.top);
            int x2 = std::min(image.cols, result.box.right);
            int y2 = std::min(image.rows, result.box.bottom);
            int width = x2 - x1;
            int height = y2 - y1;

            if (width <= 0 || height <= 0) continue;

            cv::Rect rect(x1, y1, width, height);
            cv::rectangle(image, rect, cv::Scalar(0, 255, 0), 2); // Green Box

            // Render text dynamic metadata labels safely above the boundaries
            std::ostringstream label;
            label << "Class: " << result.cls_id << " Conf: " << std::fixed << std::setprecision(2) << result.prop;
            
            int baseLine = 0;
            cv::Size label_size = cv::getTextSize(label.str(), cv::FONT_HERSHEY_SIMPLEX, 0.4, 1, &baseLine);
            
            // Prevent drawing the text background box outside the top image bounds
            int label_y = std::max(y1, label_size.height + 5);
            
            cv::rectangle(image, cv::Point(x1, label_y - label_size.height - 2),
                          cv::Point(x1 + label_size.width, label_y + baseLine),
                          cv::Scalar(0, 255, 0), -1);
                          
            cv::putText(image, label.str(), cv::Point(x1, label_y - 2),
                        cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(0, 0, 0), 1, cv::LINE_AA);
        }

        // Save output image directly to disk
        cv::imwrite(outputPath, image);

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
        setWindowTitle("RKNN Bulk Processor Dashboard");
        resize(500, 250);

        QVBoxLayout* layout = new QVBoxLayout(this);
        
        btnBulkProcess = new QPushButton("Select Images for Bulk Processing");
        btnBulkProcess->setStyleSheet("padding: 12px; font-weight: bold; font-size: 13px;");
        
        statusLabel = new QLabel("System Ready. Waiting for images...");
        statsLabel = new QLabel("Total Time: 0 ms | Avg Time/Image: 0 ms");
        statsLabel->setStyleSheet("color: #2980b9; font-weight: bold; font-size: 12px;");

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

            // Throttling updates to the UI every 10 images keeps the main event thread light
            if (processedCount % 10 == 0 || processedCount == totalImages) {
                statusLabel->setText(QString("Processing %1 of %2...").arg(processedCount).arg(totalImages));
                QCoreApplication::processEvents(); 
            }

            double timeTaken = aiEngine->processImage(inputPath, outputPath);
            totalTimeMs += timeTaken;
        }

        double avgTime = (processedCount > 0) ? (totalTimeMs / processedCount) : 0.0;
        
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

    // Initialize the RKNN Engine with your converted weights
    AIEngine engine("yolo11n.rknn"); 

    MainWindow window(&engine);
    window.show();

    return app.exec();
}