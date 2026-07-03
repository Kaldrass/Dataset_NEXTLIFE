#include <opencv2/opencv.hpp>
#include <iostream>
#include <vector>
#include <algorithm>
#include <random>
#include <map>
#include <string>

using namespace cv;
using namespace std;

// ── Helpers ────────────────────────────────────────────────────────────────────

// Test if every pixel in blockRect of `labels` equals `label`
bool isBlockFullyInLabel(const Mat& labels, const Rect& blockRect, int label) {
    Mat block = labels(blockRect);
    return countNonZero(block != label) == 0;
}

// Build a map: connected-component label → list of fully contained blocks
void getBlockMap(const Mat& labelMask, int blockSize, map<int, vector<Rect>>& labeledBlocks) {
    int numBlocksX = labelMask.cols / blockSize;
    int numBlocksY = labelMask.rows / blockSize;

    for (int y = 0; y < numBlocksY; ++y) {
        for (int x = 0; x < numBlocksX; ++x) {
            Rect rect(x * blockSize, y * blockSize, blockSize, blockSize);
            int centerLabel = labelMask.at<int>(rect.y + blockSize/2, rect.x + blockSize/2);
            if (centerLabel > 0 && isBlockFullyInLabel(labelMask, rect, centerLabel)) {
                labeledBlocks[centerLabel].push_back(rect);
            }
        }
    }
}

// ── Scramble ───────────────────────────────────────────────────────────────────

void scrambleWithKey(Mat& image, const Mat& labels, int blockSize, const string& key) {
    // 1) Group blocks by label
    map<int, vector<Rect>> labeledBlocks;
    getBlockMap(labels, blockSize, labeledBlocks);

    // 2) Seed PRNG from key
    seed_seq seed(key.begin(), key.end());
    mt19937 gen(seed);

    Mat scrambled = image.clone();

    // 3) For each label-group, shuffle & transform
    for (auto& [label, blocks] : labeledBlocks) {
        vector<Rect> shuffled = blocks;
        shuffle(shuffled.begin(), shuffled.end(), gen);

        // Copy each block to its new position
        for (size_t i = 0; i < blocks.size(); ++i) {
            image(blocks[i]).copyTo(scrambled(shuffled[i]));
        }

        // Apply independent per-block transforms
        for (auto& rect : shuffled) {
            Mat roi = scrambled(rect);

            // a) Rotation (0°, 90°, 180°, 270°)
            int rot = gen() % 4;
            switch (rot) {
                case 1: rotate(roi, roi, ROTATE_90_CLOCKWISE);       break;
                case 2: rotate(roi, roi, ROTATE_180);                 break;
                case 3: rotate(roi, roi, ROTATE_90_COUNTERCLOCKWISE); break;
                default: /* none */                                  break;
            }

            // // b) Flip: none, horizontal or vertical
            int flipType = gen() % 3;
            if (flipType == 1)      flip(roi, roi, 1);
            else if (flipType == 2) flip(roi, roi, 0);

            // c) Colour-swap: one of 6 channel permutations
            int cs = gen() % 6;
            vector<Mat> ch(3);
            split(roi, ch);
            switch (cs) {
                case 0: swap(ch[0], ch[1]); break;  // B↔G
                case 1: swap(ch[0], ch[2]); break;  // B↔R
                case 2: swap(ch[1], ch[2]); break;  // G↔R
                case 3: rotate(ch.begin(),    ch.begin()+1, ch.end());   break; // B,G,R→G,R,B
                case 4: rotate(ch.rbegin(),   ch.rbegin()+1,  ch.rend()); break; // B,G,R→R,B,G
                default: /* none */                                 break;
            }
            merge(ch, roi);
        }
    }

    image = scrambled;
}

// ── Revert ─────────────────────────────────────────────────────────────────────

void revertScrambleWithKey(Mat& image, const Mat& labels, int blockSize, const string& key) {
    map<int, vector<Rect>> labeledBlocks;
    getBlockMap(labels, blockSize, labeledBlocks);

    seed_seq seed(key.begin(), key.end());
    mt19937 gen(seed);

    Mat restored = image.clone();

    for (auto& [label, blocks] : labeledBlocks) {
        vector<Rect> shuffled = blocks;
        shuffle(shuffled.begin(), shuffled.end(), gen);

        for (size_t i = 0; i < blocks.size(); ++i) {
            // Draw transforms in same order they were applied
            int rot      = gen() % 4;
            int flipType = gen() % 3;
            int cs       = gen() % 6;

            // Extract scrambled block
            Mat tmp = image(shuffled[i]).clone();

            // 1) Undo colour-swap
            vector<Mat> ch(3);
            split(tmp, ch);
            switch (cs) {
                case 0: swap(ch[0], ch[1]); break; // B↔G
                case 1: swap(ch[0], ch[2]); break; // B↔R
                case 2: swap(ch[1], ch[2]); break; // G↔R
                case 3: rotate(ch.rbegin(), ch.rbegin()+1, ch.rend()); break; // inverse of left-rotate
                case 4: rotate(ch.begin(),    ch.begin()+1,    ch.end());   break; // inverse of right-rotate
                default: /* none */                                  break;
            }
            merge(ch, tmp);

            // 2) Undo flip (re-flipping restores)
            if (flipType == 1)      flip(tmp, tmp, 1);
            else if (flipType == 2) flip(tmp, tmp, 0);

            // 3) Undo rotation
            switch (rot) {
                case 1: rotate(tmp, tmp, ROTATE_90_COUNTERCLOCKWISE); break;
                case 2: rotate(tmp, tmp, ROTATE_180);                 break;
                case 3: rotate(tmp, tmp, ROTATE_90_CLOCKWISE);        break;
                default: /* none */                                  break;
            }

            // 4) Copy back to original location
            tmp.copyTo(restored(blocks[i]));
        }
    }

    image = restored;
}

// ── Main ───────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    if (argc != 7) {
        cout << "Usage:\n"
             << "  " << argv[0] << " <mode> <image> <mask> <block_size> <key> <output>\n"
             << "    mode       = scramble | revert\n";
        return -1;
    }

    string mode       = argv[1];
    string imagePath  = argv[2];
    string maskPath   = argv[3];
    int    blockSize  = atoi(argv[4]);
    string key        = argv[5];
    string outputPath = argv[6];

    // Load
    Mat image = imread(imagePath, IMREAD_COLOR);
    if (image.empty()) { cerr << "Could not load image.\n"; return -1; }

    Mat mask = imread(maskPath, IMREAD_GRAYSCALE);
    if (mask.empty())  { cerr << "Could not load mask.\n";  return -1; }

    // Prep mask → labels
    resize(mask, mask, image.size(), 0, 0, INTER_NEAREST);
    threshold(mask, mask, 128, 255, THRESH_BINARY);
    Mat labels;
    connectedComponents(mask, labels, 8, CV_32S);

    // Scramble or revert
    if (mode == "scramble") {
        scrambleWithKey(image, labels, blockSize, key);
    }
    else if (mode == "revert") {
        revertScrambleWithKey(image, labels, blockSize, key);
    }
    else {
        cerr << "Unknown mode: " << mode << "\n";
        return -1;
    }

    // Save and exit
    imwrite(outputPath, image);
    cout << "✅ " << mode << "d image saved to: " << outputPath << "\n";
    return 0;
}
