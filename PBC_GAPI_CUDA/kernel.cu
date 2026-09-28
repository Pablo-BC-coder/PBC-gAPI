#include <cuda_runtime.h>
#include <iostream>
#include <windows.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <array>
#include <cstring>

#include "terminalPBC_API.cpp"

// Estrutura para empacotar os triângulos processados para a GPU
struct ProcessedTriangle {
    float x0, y0, z0, iz0; VaryData vary0;
    float x1, y1, z1, iz1; VaryData vary1;
    float x2, y2, z2, iz2; VaryData vary2;
};

// Estrutura para o leitor de OBJ suportar normais nativas do arquivo
struct ObjFace {
    int v[3];
    int vn[3]; // Índices das normais extraídos do arquivo (-1 se ausentes)
};

// === KERNELS DA GPU ===

// Limpa o buffer unificado de 64 bits de uma só vez na GPU
__global__ void clearScreen64Kernel(uint64_t* frame_buffer, int size, uint64_t clear_value) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < size) {
        frame_buffer[i] = clear_value;
    }
}

// O novo formatador: não precisa processar matemática de float, apenas extrai os 32 bits inferiores da cor
__global__ void formatHDC64Kernel(uint64_t* frame_buffer, uint32_t* out_pixels, int size) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < size) {
        // Isola os 32 bits inferiores que contêm o RGB pronto
        out_pixels[i] = (uint32_t)(frame_buffer[i] & 0xFFFFFFFFULL);
    }
}

// RASTERIZADOR DE ALTA PERFORMANCE (1 Bloco = 1 Triângulo)
__global__ void renderTriangles64Kernel(Framebuffer fb, uint64_t* frame_buffer, ProcessedTriangle* triangles, int numTriangles) {
    int t = blockIdx.x; // Cada bloco cuida de um triângulo
    if (t >= numTriangles) return;

    ProcessedTriangle tri = triangles[t];

    float x = tri.x0, y = tri.y0;
    float x1 = tri.x1, y1 = tri.y1;
    float x2 = tri.x2, y2 = tri.y2;

    // Bounding Box extremamente apertada para evitar processar a tela toda
    int minX = max(0, (int)fminf(x, fminf(x1, x2)));
    int maxX = min(fb.width - 1, (int)fmaxf(x, fmaxf(x1, x2)));
    int minY = max(0, (int)fminf(y, fminf(y1, y2)));
    int maxY = min(fb.height - 1, (int)fmaxf(y, fmaxf(y1, y2)));

    int boxWidth = maxX - minX + 1;
    int boxHeight = maxY - minY + 1;
    int totalPixels = boxWidth * boxHeight;
    if (totalPixels <= 0) return;

    float area_tri = 0.5f * abs(x * (y1 - y2) + x1 * (y2 - y) + x2 * (y - y1));
    if (area_tri == 0.0f) return;

    int fixed_x = (int)(x * 16.0f);   int fixed_y = (int)(y * 16.0f);
    int fixed_x1 = (int)(x1 * 16.0f); int fixed_y1 = (int)(y1 * 16.0f);
    int fixed_x2 = (int)(x2 * 16.0f); int fixed_y2 = (int)(y2 * 16.0f);

    int delta_xa = fixed_x2 - fixed_x1; int delta_ya = fixed_y2 - fixed_y1;
    int delta_xb = fixed_x - fixed_x2;  int delta_yb = fixed_y - fixed_y2;
    int delta_xc = fixed_x1 - fixed_x;  int delta_yc = fixed_y1 - fixed_y;

    // Threads do bloco dividem os pixels da Bounding Box de forma paralela
    for (int p = threadIdx.x; p < totalPixels; p += blockDim.x) {
        int j = minX + (p % boxWidth);
        int i = minY + (p / boxWidth);

        int fixed_j = j << 4;
        int fixed_i = i << 4;

        int edge_a = ((fixed_j - fixed_x1) * delta_ya >> 4) - ((fixed_i - fixed_y1) * delta_xa >> 4);
        int edge_b = ((fixed_j - fixed_x2) * delta_yb >> 4) - ((fixed_i - fixed_y2) * delta_xb >> 4);
        int edge_c = ((fixed_j - fixed_x) * delta_yc >> 4) - ((fixed_i - fixed_y) * delta_xc >> 4);

        bool top_left_edge_a = (edge_a == 0 && (delta_ya < 0 || (delta_ya == 0 && delta_xa > 0)));
        bool top_left_edge_b = (edge_b == 0 && (delta_yb < 0 || (delta_yb == 0 && delta_xb > 0)));
        bool top_left_edge_c = (edge_c == 0 && (delta_yc < 0 || (delta_yc == 0 && delta_xc > 0)));
        bool insideA = edge_a < 0 || (edge_a == 0 && top_left_edge_a);
        bool insideB = edge_b < 0 || (edge_b == 0 && top_left_edge_b);
        bool insideC = edge_c < 0 || (edge_c == 0 && top_left_edge_c);

        if (insideA && insideB && insideC) {

            float a = 0.5f * abs(j * (y1 - y2) + x1 * (y2 - i) + x2 * (i - y1)) / area_tri;
            float b = 0.5f * abs(x * (i - y2) + j * (y2 - y) + x2 * (y - i)) / area_tri;
            float c = 0.5f * abs(x * (y1 - i) + x1 * (i - y) + j * (y - y1)) / area_tri;

            float z_pixel = 1.0f / (a * tri.iz0 + b * tri.iz1 + c * tri.iz2);
            if (z_pixel <= 0.0f) continue;

            float dp_a = z_pixel * a * tri.iz0;
            float dp_b = z_pixel * b * tri.iz1;
            float dp_c = z_pixel * c * tri.iz2;

            VaryData vary_in;
            for (size_t v = 0; v < 8; ++v) {
                vary_in.v[v] = (tri.vary0.v[v] * dp_a) + (tri.vary1.v[v] * dp_b) + (tri.vary2.v[v] * dp_c);
            }

            Color final_col = SimplePhongTexture2(vary_in, fb);

            // Converte os canais de cores para inteiros
            uint32_t col_r = (uint32_t)(fmaxf(0.0f, fminf(1.0f, final_col.r)) * 255.0f);
            uint32_t col_g = (uint32_t)(fmaxf(0.0f, fminf(1.0f, final_col.g)) * 255.0f);
            uint32_t col_b = (uint32_t)(fmaxf(0.0f, fminf(1.0f, final_col.b)) * 255.0f);
            uint32_t packed_color = (col_r << 16) | (col_g << 8) | col_b;

            // Transforma o float Z de forma estável para ordenação inteira de 32 bits
            uint32_t packed_depth = __float_as_uint(z_pixel);

            // Monta o payload de 64 bits: Profundidade no topo, Cor na base
            uint64_t payload = ((uint64_t)packed_depth << 32) | packed_color;

            int fb_index = i * fb.width + j;

            // OPERAÇÃO ATÔMICA UNIFICADA: Atualiza Z e Cor simultaneamente sem Race Conditions!
            atomicMin((unsigned long long*) & frame_buffer[fb_index], (unsigned long long)payload);
        }
    }
}

// === MATEMÁTICA NA CPU ===
__host__ Vec3 rotate3D(Vec3 v, float angleX, float angleY, float angleZ) {
    float cosX = std::cos(angleX), sinX = std::sin(angleX);
    float y1 = v.y * cosX - v.z * sinX;
    float z1 = v.y * sinX + v.z * cosX;

    float cosY = std::cos(angleY), sinY = std::sin(angleY);
    float x2 = v.x * cosY + z1 * sinY;
    float z2 = -v.x * sinY + z1 * cosY;

    float cosZ = std::cos(angleZ), sinZ = std::sin(angleZ);
    float x3 = x2 * cosZ - y1 * sinZ;
    float y3 = x2 * sinZ + y1 * cosZ;

    return { x3, y3, z2 };
}

__host__ Vec3 crossProduct(Vec3 a, Vec3 b) {
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

__host__ Vec3 normalize(Vec3 v) {
    float length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (length == 0.0f) return { 0,0,0 };
    return { v.x / length, v.y / length, v.z / length };
}

// === PARSER COMPLEXO DE OBJ COM SUPORTE A NORMAIS VÉRTICE (vn) ===
bool loadOBJ(const char* path, std::vector<Vec3>& out_vertices, std::vector<Vec3>& out_normals, std::vector<ObjFace>& out_faces) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream p_stream(line);
        std::string prefix;
        p_stream >> prefix;

        if (prefix == "v") {
            Vec3 v; p_stream >> v.x >> v.y >> v.z;
            out_vertices.push_back(v);
        }
        else if (prefix == "vn") {
            Vec3 vn; p_stream >> vn.x >> vn.y >> vn.z;
            out_normals.push_back(vn);
        }
        else if (prefix == "f") {
            ObjFace face;
            for (int i = 0; i < 3; ++i) {
                std::string segment;
                p_stream >> segment;
                face.vn[i] = -1; // Flag padrão se o arquivo não tiver normais

                size_t pos1 = segment.find('/');
                if (pos1 != std::string::npos) {
                    face.v[i] = std::stoi(segment.substr(0, pos1)) - 1;
                    size_t pos2 = segment.find('/', pos1 + 1);
                    if (pos2 != std::string::npos) {
                        std::string vnPart = segment.substr(pos2 + 1);
                        if (!vnPart.empty()) face.vn[i] = std::stoi(vnPart) - 1;
                    }
                }
                else {
                    face.v[i] = std::stoi(segment) - 1;
                }
            }
            out_faces.push_back(face);
        }
    }
    return true;
}

bool g_running = true;
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_DESTROY) { g_running = false; PostQuitMessage(0); return 0; }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int main() {
    const int WIDTH = 640;
    const int HEIGHT = 480;

    std::vector<Vec3> obj_vertices;
    std::vector<Vec3> obj_normals;
    std::vector<ObjFace> obj_faces;

    if (!loadOBJ("suzanne.obj", obj_vertices, obj_normals, obj_faces)) {
        std::cerr << "ERRO: O arquivo suzanne.obj nao foi encontrado!" << std::endl;
        return -1;
    }
    int numTriangles = obj_faces.size();

    // 1. Alocando o novo Buffer Unificado de 64 bits para a GPU
    uint64_t* d_frame_buffer;
    uint32_t* d_display_pixels;
    ProcessedTriangle* d_triangles;

    cudaMallocManaged(&d_display_pixels, WIDTH * HEIGHT * sizeof(uint32_t));
    cudaMallocManaged(&d_frame_buffer, WIDTH * HEIGHT * sizeof(uint64_t));
    cudaMallocManaged(&d_triangles, numTriangles * sizeof(ProcessedTriangle));

    // Instancia o objeto de compatibilidade da API
    Framebuffer fb(WIDTH, HEIGHT, nullptr, nullptr);

    HINSTANCE hInstance = GetModuleHandle(NULL);
    // WNDCLASSW wc = { 0 }; wc.lpfnWndProc = WindowProc; wc.hInstance = hInstance; wc.lpszClassName = L"CUDASmooth"; // Escreve na tela por cima do sistema
    WNDCLASSW wc = { 0 }; wc.lpfnWndProc = WindowProc; wc.hInstance = hInstance; wc.lpszClassName = L"CUDAClass";
    RegisterClassW(&wc);

    RECT rect = { 0, 0, WIDTH, HEIGHT }; AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowExW(0, L"CUDAClass", L"CUDA Renderer - Smooth Shading & Bounding Box 60 FPS+",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
        NULL, NULL, hInstance, NULL);
    ShowWindow(hwnd, SW_SHOW);

    // Configurando o valor inicial de limpeza (Fundo cinza escuro, Z inicial distante de 1000.0f)
    float clear_depth_val = 1000.0f;
    uint32_t clear_depth_bits;
    std::memcpy(&clear_depth_bits, &clear_depth_val, sizeof(float)); // Engana o C++ na CPU
    uint32_t clear_color_bits = (25 << 16) | (25 << 8) | 25;
    uint64_t clear_value = ((uint64_t)clear_depth_bits << 32) | clear_color_bits;

    float rotX = 0.0f, rotY = 0.0f, rotZ = 0.0f;
    MSG msg = { 0 };

    while (g_running) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg); DispatchMessage(&msg);
        }
        else {
            int totalPixels = WIDTH * HEIGHT;
            int blockSize1D = 256;
            int numBlocks1D = (totalPixels + blockSize1D - 1) / blockSize1D;

            // Limpa o Buffer Unificado instantaneamente por meio da GPU
            clearScreen64Kernel << <numBlocks1D, blockSize1D >> > (d_frame_buffer, totalPixels, clear_value);
            cudaDeviceSynchronize();

            int triIndex = 0;
            for (const auto& face : obj_faces) {
                Vec3 v0 = obj_vertices[face.v[0]];
                Vec3 v1 = obj_vertices[face.v[1]];
                Vec3 v2 = obj_vertices[face.v[2]];

                Vec3 n0, n1, n2;
                // Aplica Smooth Shading perfeito se as normais existirem no arquivo
                if (face.vn[0] != -1 && face.vn[1] != -1 && face.vn[2] != -1) {
                    n0 = obj_normals[face.vn[0]];
                    n1 = obj_normals[face.vn[1]];
                    n2 = obj_normals[face.vn[2]];
                }
                else {
                    // Fallback dinâmico caso falte informação de iluminação no modelo
                    Vec3 edge1 = { v1.x - v0.x, v1.y - v0.y, v1.z - v0.z };
                    Vec3 edge2 = { v2.x - v0.x, v2.y - v0.y, v2.z - v0.z };
                    n0 = n1 = n2 = normalize(crossProduct(edge1, edge2));
                }

                struct PVert { float x, y, z, iz; VaryData vary; } pVerts[3];
                Vec3 verts[3] = { v0, v1, v2 }; Vec3 norms[3] = { n0, n1, n2 };

                for (int i = 0; i < 3; ++i) {
                    Vec3 wPos = rotate3D(verts[i], rotX, rotY, rotZ);
                    Vec3 wNorm = rotate3D(norms[i], rotX, rotY, rotZ);

                    Vec3 cPos = wPos; cPos.z += 3.5f; // Posicionamento da câmera
                    if (cPos.z < 0.1f) cPos.z = 0.1f;

                    float fov = 380.0f;
                    float screenX = (cPos.x / cPos.z) * fov + (WIDTH / 2.0f);
                    float screenY = (cPos.y / cPos.z) * fov + (HEIGHT / 2.0f);

                    VaryData vary;
                    vary.v[0] = wNorm.x; vary.v[1] = wNorm.y; vary.v[2] = wNorm.z;
                    vary.v[3] = -cPos.x; vary.v[4] = -cPos.y; vary.v[5] = -cPos.z;
                    vary.v[6] = 0.0f; vary.v[7] = 0.0f;

                    pVerts[i] = { screenX, screenY, cPos.z, 1.0f / cPos.z, vary };
                }

                float area = (pVerts[1].x - pVerts[0].x) * (pVerts[2].y - pVerts[0].y) -
                    (pVerts[2].x - pVerts[0].x) * (pVerts[1].y - pVerts[0].y);

                if (area < 0.0f) {
                    d_triangles[triIndex++] = {
                        pVerts[0].x, pVerts[0].y, pVerts[0].z, pVerts[0].iz, pVerts[0].vary,
                        // ATENÇÃO: O Vértice 2 vem primeiro aqui!
                        pVerts[2].x, pVerts[2].y, pVerts[2].z, pVerts[2].iz, pVerts[2].vary,
                        // O Vértice 1 vem por último para inverter a ordem geométrica!
                        pVerts[1].x, pVerts[1].y, pVerts[1].z, pVerts[1].iz, pVerts[1].vary
                    };
                }
            }

            // LANÇAMENTO DE ALTA PERFORMANCE DA GEOMETRIA
            if (triIndex > 0) {
                int threadsPerBlock = 256;
                renderTriangles64Kernel << <triIndex, threadsPerBlock >> > (fb, d_frame_buffer, d_triangles, triIndex);
                cudaDeviceSynchronize();
            }

            // GPU extrai a imagem final de forma instantânea
            formatHDC64Kernel << <numBlocks1D, blockSize1D >> > (d_frame_buffer, d_display_pixels, totalPixels);
            cudaDeviceSynchronize();

            HDC hdc = GetDC(hwnd);
            BITMAPINFO bmi = { 0 };
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = WIDTH; bmi.bmiHeader.biHeight = -HEIGHT;
            bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32; bmi.bmiHeader.biCompression = BI_RGB;
            SetDIBitsToDevice(hdc, 0, 0, WIDTH, HEIGHT, 0, 0, 0, HEIGHT, d_display_pixels, &bmi, DIB_RGB_COLORS);
            ReleaseDC(hwnd, hdc);

            rotX += 0.015f; rotY += 0.02f; rotZ += 0.005f;
        }
    }

    cudaFree(d_frame_buffer);
    cudaFree(d_triangles);
    cudaFree(d_display_pixels);
    return 0;
}