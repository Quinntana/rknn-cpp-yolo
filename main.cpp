// =========================================================================
// 1. BACKEND & STANDARD HEADERS FIRST (Prevents Qt 'emit' macro collisions)
// =========================================================================
#include <opencv2/opencv.hpp>
#include <iostream>
#include <iomanip>

// Include the RKNN model header from the backend before Qt defines 'emit'
#include "rknn_model.h"

// ==========================================
// 2. INTERFACE & GUI MODULES (Qt Framework)
// ==========================================
#include <QApplication>
#include <QWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QLabel>
#include <QFileDialog>
#include <QDir>
#include <QDateTime>
#include <QCoreApplication>

// ==========================================
// 3. THE AI MODULE (RKNN NPU Backend Wrapper)
// ==========================================
class AIEngine {
private:
    rknn_model* model;
    int ctx_index;

public:
    AIEngine(const std::string& modelPath) {
        model = new rknn_model(modelPath);
        ctx_index = 0; // Defaulting to the first NPU core context
        std::cout << ">> AI Engine Initialized (6 TOPS Hardware NPU Active)." << std::endl;
    }

    ~AIEngine() {
        delete model;
    }

    double processImage(const std::string& inputPath, const std::string& outputPath) {
        cv::Mat image = cv::imread(inputPath);
        if (image.empty()) return 0.0;
        
        double start_time = cv::getTickCount();

        object_detect_result_list od_results;
        
        // Execute hardware accelerated inference loop
        int ret = model->run_inference(image, ctx_index, &od_results);

        double end_time = cv::getTickCount();

        if (ret < 0) {
            std::cerr << "rknn_run fail! ret=" << ret << std::endl;
            return 0.0;
        }

        // Draw bounding boxes efficiently using direct scalar overlays
        for (int i = 0; i < od_results.count; ++i) {
            const auto& result = od_results.results[i];

            int x1 = std::max(0, result.box.left);
            int y1 = std::max(0, result.box.top);
            int x2 = std::min(image.cols, result.box.right);
            int y2 = std::min(image.rows, result.box.bottom);
            int width = x2 - x1;
            int height = y2 - y1;

            if (width <= 0 || height <= 0) continue;

            // Draw only the structural bounding rectangle
            cv::rectangle(image, cv::Rect(x1, y1, width, height), cv::Scalar(0, 255, 0), 2);
            
            // OPTIMIZATION: Bypassed heavy cv::getTextSize font math to minimize processing overhead.
            // Directly overlay basic numerical IDs directly above the bounds.
            std::string text_label = "ID:" + std::to_string(result.cls_id) + " P:" + std::to_string(static_cast<int>(result.prop * 100));
            cv::putText(image, text_label, cv::Point(x1, std::max(y1 - 4, 12)),
                        cv::FONT_HERSHEY_PLAIN, 0.8, cv::Scalar(0, 255, 0), 1, cv::LINE_AA);
        }

        cv::imwrite(outputPath, image);

        return ((end_time - start_time) / cv::getTickFrequency()) * 1000.0;
    }
};

// ==========================================
// 4. THE GUI MODULE (Separated UI Dashboard)
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

        // Freeze the UI entirely before entering the core processing pipeline
        btnBulkProcess->setEnabled(false); 
        statusLabel->setText(QString("Inference in progress... Processing %1 files.").arg(totalImages));
        
        // Force a single initial repaint so the user sees the freeze notice
        QCoreApplication::processEvents(); 

        for (const QString& qFilePath : fileNames) {
            std::string inputPath = qFilePath.toStdString();
            
            QFileInfo fileInfo(qFilePath);
            std::string outputPath = folderName.toStdString() + "/" + fileInfo.fileName().toStdString();

            processedCount++;

            // OPTIMIZATION: Completely removed QCoreApplication::processEvents() from here.
            // This prevents Qt from yielding CPU execution control back to the window manager mid-run.

            double timeTaken = aiEngine->processImage(inputPath, outputPath);
            totalTimeMs += timeTaken;
        }

        double avgTime = (processedCount > 0) ? (totalTimeMs / processedCount) : 0.0;
        
        // Re-enable and paint updates once calculations are complete
        statusLabel->setText(QString("Finished! Saved to folder: %1").arg(folderName));
        statsLabel->setText(QString("Total Time: %1 ms | Avg Time/Image: %2 ms")
                                .arg(totalTimeMs, 0, 'f', 2)
                                .arg(avgTime, 0, 'f', 2));

        btnBulkProcess->setEnabled(true);
    }
};

// ==========================================
// 3. MAIN APPLICATION ENTRY POINT
// ==========================================
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    AIEngine engine("yolo11n.rknn"); 

    MainWindow window(&engine);
    window.show();

    return app.exec();
}