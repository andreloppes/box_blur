#include <iostream>
#include <vector>
#include <condition_variable>
#include <mutex>
#include <string>
#include <filesystem>
#include <chrono>
#include <thread>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

using namespace std;

// CONSTANTS
static const string INPUT_DIRECTORY = "../input";
static const string OUTPUT_DIRECTORY = "../output";
static const int FILTER_SIZE = 5;
static const int NUM_CHANNELS = 3;
static const unsigned SLEEP_TIME = 0; // ms

// Image type definition
typedef vector<vector<uint8_t>> single_channel_image_t;
typedef array<single_channel_image_t, NUM_CHANNELS> image_t;

// Mutex para proteger os recursos compartilhados
std::mutex m;

// Variável de condição que indica que existe espaço disponível no buffer
// O consumidor utiliza essa variável de condição para notificar o produtor que a fila não está cheia
std::condition_variable space_available;

// Variável de condição que indica que existem dados disponíveis no buffer
// O produtor utiliza essa variável de condição para notificar o consumidor que a fila não está vazia
std::condition_variable data_available;


// Numero de threads produtoras e consumidoras a serem criadas no main()
static const unsigned NUM_PRODUCERS = 1;
//static const unsigned NUM_CONSUMERS = 10;


//  =========================  Circular buffer  ============================================
// Código que implementa um buffer circular
static const unsigned BUFFER_SIZE = 1000;
string buffer[BUFFER_SIZE];
static unsigned counter = 0;
unsigned in = 0, out = 0;
bool flag = false;
void add_buffer(string str)
{
  buffer[in] = str;
  in = (in+1) % BUFFER_SIZE;
  counter++;
}

string get_buffer()
{
  string v;
  v = buffer[out];
  out = (out+1) % BUFFER_SIZE;
  counter--;
  return v;
}

image_t load_image(const string &filename)
{
    int width, height, channels;

    unsigned char *data = stbi_load(filename.c_str(), &width, &height, &channels, 0);
    if (!data)
    {
        throw runtime_error("Failed to load image " + filename);
    }

    image_t result;
    for (int i = 0; i < NUM_CHANNELS; ++i)
    {
        result[i] = single_channel_image_t(height, vector<uint8_t>(width));
    }

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            for (int c = 0; c < NUM_CHANNELS; ++c)
            {
                result[c][y][x] = data[(y * width + x) * NUM_CHANNELS + c];
            }
        }
    }
    stbi_image_free(data);
    return result;
}

void write_image(const string &filename, const image_t &image)
{
    int channels = image.size();
    int height = image[0].size();
    int width = image[0][0].size();

    vector<unsigned char> data(height * width * channels);

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            for (int c = 0; c < channels; ++c)
            {
                data[(y * width + x) * channels + c] = image[c][y][x];
            }
        }
    }
    if (!stbi_write_png(filename.c_str(), width, height, channels, data.data(), width * channels))
    {
        throw runtime_error("Failed to write image");
    }
}

single_channel_image_t apply_box_blur(const single_channel_image_t &image, const int filter_size)
{
    // Get the dimensions of the input image
    int width = image[0].size();
    int height = image.size();
    

    // Create a new image to store the result
    single_channel_image_t result(height, vector<uint8_t>(width));

    // Calculate the padding size for the filter
    int pad = filter_size / 2;

    // Loop through the image pixels, skipping the border pixels
    for(int row = pad; row < height-pad; row++){
        for(int col = pad; col < width-pad; col++){ 
        int sum = 0;
        int average = 0;

             // Loop through the filter's rows and columns
            for(int k_row = -pad; k_row < (pad + 1); k_row++){
                for(int k_col = -pad; k_row < (pad + 1); k_row++){
                    // Add the corresponding image pixel value to the sum
                    sum += image[row + k_row][col + k_col];
                }
            }
            // Calculate the average value for the current pixel
            average = sum / (filter_size * filter_size);

            // Assign the average value to the corresponding pixel in the result image
            result[row][col] = average;
        }
    }
        // Copy the border pixels from the input image to the result image
    for (int row = 0; row < height; row++){
        for (int col = 0; col < pad; col++){
            result[row][col] = image[row][col];
            result[row][width - col - 1] = image[row][width - col - 1];
     }
    }

    for (int col = 0; col < width; col++){
        for(int row = 0; row < pad; row++){
            result[row][col] = image[row][col];
            result[height - row - 1][col] = image[height - row - 1][col];
        }
    }
    return result;
}

// Consumer
void consumer_func(const unsigned id)
{
	while (true)
	{
		// Cria um objeto do tipo unique_lock que no construtor chama m.lock()
		std::unique_lock<std::mutex> lock(m);
		
		// Verifica se o buffer está vazio e, caso afirmativo, espera notificação de "dado disponível no buffer"
		while (counter == 0)
		{
			data_available.wait(lock); // Espera por dado disponível
			// Lembre-se que a função wait() invoca m.unlock() antes de colocar a thread em estado de espera para que o produtor consiga adquirir a posse do mutex m e produzir dados
			// Quando a thread é acordada, a função wait() invoca m.lock() retomando a posse do mutex m
		}



		// O buffer não está mais vazio, então consome um elemento
		string i = get_buffer();
		
        clog << "Processing image: " << i << endl;
        image_t input_image = load_image(i);
        image_t output_image;
        for (int i = 0; i < NUM_CHANNELS; ++i)
        {
            output_image[i] = apply_box_blur(input_image[i], FILTER_SIZE);
        }
        string output_image_path = i.replace(i.find(INPUT_DIRECTORY), INPUT_DIRECTORY.length(), OUTPUT_DIRECTORY);
        write_image(output_image_path, output_image);
        std::cout << "Consumer " << id << " - Buffer counter: " << counter << std::endl;
		space_available.notify_one();

        if(counter == 0){
            exit(0);
        }
		// Verifica a integridade do buffer, ou seja, verifica se o número de elementos do buffer (counter)
		// é maior ou igual a zero
		assert(counter >= 0);
	}  // Fim de escopo -> o objeto lock será destruído e invocará m.unlock(), liberando o mutex m
}



int main(int argc, char *argv[])
{
    if (!filesystem::exists(INPUT_DIRECTORY))
    {
        cerr << "Error, " << INPUT_DIRECTORY << " directory does not exist" << endl;
        return 1;
    }

    if (!filesystem::exists(OUTPUT_DIRECTORY))
    {
        if (!filesystem::create_directory(OUTPUT_DIRECTORY))
        {
            cerr << "Error creating" << OUTPUT_DIRECTORY << " directory" << endl;
            return 1;
        }
    }

    if (!filesystem::is_directory(OUTPUT_DIRECTORY))
    {
        cerr << "Error there is a file named " << OUTPUT_DIRECTORY << ", it should be a directory" << endl;
        return 1;
    }

    auto start_time = chrono::high_resolution_clock::now();
    int i = 0;
    std::vector<std::thread> consumers;
   
    for (auto &file : filesystem::directory_iterator{INPUT_DIRECTORY})
    {
        // Cria um objeto do tipo unique_lock que no construtor chama m.lock()
		std::unique_lock<std::mutex> lock(m);

		// Verifica se o buffer está cheio e, caso afirmativo, espera notificação de "espaço disponível no buffer"
		while (counter == BUFFER_SIZE)
		{			
			space_available.wait(lock); // Espera por espaço disponível 
			// Lembre-se que a função wait() invoca m.unlock() antes de colocar a thread em estado de espera para que o consumidor consiga adquirir a posse do mutex m	e consumir dados
			// Quando a thread é acordada, a função wait() invoca m.lock() retomando a posse do mutex m
		}

		// O buffer não está mais cheio, então produz um elemento
		string input_image_path = file.path().string();
        add_buffer(input_image_path);
        
		
		// Notifica o consumirod que existem dados a serem consumidos no buffer
		data_available.notify_one();
		
		// (Opicional) dorme por SLEEP_TIME milissegundos
		if (SLEEP_TIME > 0)
			std::this_thread::sleep_for(std::chrono::milliseconds(SLEEP_TIME));

		// Verifica a integridade do buffer, ou seja, verifica se o número de elementos do buffer (counter)
		// é menor ou igual a BUFFER_SIZE
		assert(counter <= BUFFER_SIZE);		
    }
    int NUM_CONSUMERS = INPUT_DIRECTORY.length();

    for (unsigned i =0; i < NUM_CONSUMERS; ++i)
	{
		consumers.push_back(std::thread(consumer_func, i));
 
	}

for (int i=0; i< consumers.size(); ++i)
	{
		consumers[i].join();
	}

    auto end_time = chrono::high_resolution_clock::now();
    auto elapsed_time = chrono::duration_cast<chrono::milliseconds>(end_time - start_time);
    cout << "Elapsed time: " << elapsed_time.count() << " ms" << endl;

    return 0;
}
