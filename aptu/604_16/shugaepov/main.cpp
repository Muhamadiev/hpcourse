#include <cstdio>
#include <cstdlib>
#include <random>
#include <thread>
#include <iostream>
#include <fstream>
#include <cstring>
#include "tbb/flow_graph.h"

using namespace tbb::flow;
using namespace std;

typedef pair<size_t, size_t> point;
typedef vector<point> points;

const int MAX_INTENSITY = 255;

class Image {
    size_t W;
    size_t H;
    vector<vector<int>> _data;

public:
    Image(size_t width, size_t height)
            : W(width)
            , H(height)
    {
        _data.assign(H, vector<int>(W));
    }

    size_t getW() const {
        return W;
    }

    size_t getH() const {
        return H;
    }

    int get(size_t i, size_t j) const
    {
        return _data[i][j];
    }

    void set(size_t i, size_t j, int value)
    {
        _data[i][j] = value;
    }

};


void fill_random(Image &image) {
    for (size_t i = 0; i < image.getH(); ++i)
        for (size_t j = 0; j < image.getW(); ++j)
            image.set(i, j, rand() % (MAX_INTENSITY + 1));
}

points min_intensity_points(const Image &image) {
    int min_intensity = MAX_INTENSITY;
    for (size_t i = 0; i < image.getH(); ++i)
        for (size_t j = 0; j < image.getW(); ++j)
            min_intensity = min(image.get(i,j), min_intensity);

    points result;
    for (size_t i = 0; i < image.getH(); ++i)
        for (size_t j = 0; j < image.getW(); ++j)
            if (image.get(i,j) == min_intensity)
                result.push_back(make_pair(i, j));

    return result;
}

points max_intensity_points(const Image &image) {
    int max_intensity = 0;
    for (size_t i = 0; i < image.getH(); ++i)
        for (size_t j = 0; j < image.getW(); ++j)
            max_intensity = max(image.get(i, j), max_intensity);

    points result;
    for (size_t i = 0; i < image.getH(); ++i)
        for (size_t j = 0; j < image.getW(); ++j)
            if (image.get(i,j) == max_intensity)
                result.push_back(make_pair(i, j));

    return result;
}

float mean_intensity(const Image &image) {
    float result = 0.f;
    for (size_t i = 0; i < image.getH(); ++i)
        for (size_t j = 0; j < image.getW(); ++j)
            result += image.get(i,j);

    return result / (image.getW() * image.getH());
}

void invert(Image &image) {
    for (size_t i = 0; i < image.getH(); ++i)
        for (size_t j = 0; j < image.getW(); ++j) {
            int val = image.get(i, j);
            image.set(i, j, MAX_INTENSITY - val);
        }
}

points target_intensity_points(const Image &image, int target) {
    points result;
    for (size_t i = 0; i < image.getH(); ++i)
        for (size_t j = 0; j < image.getW(); ++j)
            if (image.get(i,j) == target)
                result.push_back(make_pair(i, j));

    return result;
}

void highlight_features(Image &image, points features) {
    for (pair<size_t, size_t> f: features) {
        size_t y = f.first;
        size_t x = f.second;
        for (int i = (int) x - 1; i <= x + 1; ++i)
            for (int j = (int) y - 1; j <= y + 1; ++j)
                if (i >= 0 && i < image.getH() && j >= 0 && j < image.getW())
                    image.set(i, j, MAX_INTENSITY);
    }
}

size_t images_limit = 10;
string log_file = "log.txt";
int target_intensity = 127;

void run()
{
    ofstream log(log_file);
    size_t width = 256;
    size_t height = 256;

    graph g;
    size_t total_images = 1000;
    size_t counter = 0;
    auto generator = [&](Image& output) -> bool {
        if (counter >= total_images) {
            return false;
        }
        counter++;
        Image image(width, height);
        fill_random(image);
        output = image;
        return true;
    };
    source_node<Image> source(g, generator);
    limiter_node<Image> limit(g, images_limit);
    broadcast_node<Image> broadcast(g);


    auto min_body = [](const Image& image) -> points {
        return min_intensity_points(image);
    };

    auto max_body = [](const Image& image) -> points {
        return max_intensity_points(image);
    };

    auto equal_body = [](const Image& image) -> points {
        return target_intensity_points(image, target_intensity);
    };

    function_node<Image, points, rejecting> min_node(g, unlimited, min_body);

    function_node<Image, points, rejecting> max_node(g, unlimited, max_body);

    function_node<Image, points, rejecting> equal_node(g, unlimited, equal_body);

    join_node<tuple<Image, points, points, points>, queueing> join(g);

    auto highlight_body = [&log](tuple<Image, points, points, points> input) -> Image {
        Image image = get<0>(input);
        highlight_features(image, get<1>(input));
        highlight_features(image, get<2>(input));
        highlight_features(image, get<3>(input));
        return image;
    };

    function_node<tuple<Image, points, points, points>, Image, rejecting> highlight(g, unlimited, highlight_body);
    broadcast_node<Image> broadcast2(g);

    auto mean_body = [](const Image& image)->float {
        return mean_intensity(image);
    };

    function_node<Image, float, rejecting> mean_node(g, unlimited, mean_body);

    auto invert_body = [](const Image& image)->Image {
        Image result(image);
        invert(result);
        return result;
    };

    function_node<Image, Image, rejecting> invert_node(g, unlimited, invert_body);

    auto output_body = [&log](float mean)->continue_msg {
        log << "mean intensity " << mean << endl;
        return continue_msg();
    };

    function_node<float, continue_msg, rejecting> output(g, unlimited, output_body);

    make_edge(source, limit);
    make_edge(limit, broadcast);

    make_edge(broadcast, equal_node);
    make_edge(broadcast, max_node);
    make_edge(broadcast, min_node);

    make_edge(broadcast, input_port<0>(join));
    make_edge(max_node, input_port<1>(join));
    make_edge(min_node, input_port<2>(join));
    make_edge(equal_node, input_port<3>(join));

    make_edge(join, highlight);
    make_edge(highlight, broadcast2);
    make_edge(broadcast2, mean_node);
    make_edge(broadcast2, invert_node);
    make_edge(mean_node, output);
    make_edge(output, limit.decrement);

    source.activate();
    g.wait_for_all();
    log.close();
    cout << "ok" << endl;
}

int main(int argc, char* argv[]) {
    cout << "Usage: -b <target intensity in [0, 255]>  -l <images limit> -f <log file>" << endl;

    for (int i = 0; i < argc; ++i) {
        if (strcmp(argv[i], "-b") == 0) {
            target_intensity = stoi(argv[++i]);
            cout << "Target intensity = " << target_intensity << endl;
            continue;
        }
        if (strcmp(argv[i], "-l") == 0) {
            images_limit = (size_t) stoi(argv[++i]);
            cout << "Images limit = " << images_limit << endl;
            continue;
        }
        if (strcmp(argv[i], "-f") == 0) {
            log_file = argv[++i];
            continue;
        }
    }
    run();
    return 0;
}