#include "Windows.h"
#include "GarrysMod/Lua/Interface.h"
#include "html/IHtmlSystem.h"
#include "stdio.h"

#include "glad/glad.h"
#include "GLFW/glfw3.h"
#define GLFW_EXPOSE_NATIVE_WIN32
#include "GLFW/glfw3native.h"

#include <thread>

IHtmlSystem* g_HtmlSystem;
HWND gmod_window;

class GmodOverlayClientListener : public IHtmlClientListener
{
public:
    GmodOverlayClientListener() : loading(true) {};
    ~GmodOverlayClientListener() {};

    virtual void OnAddressChange(const char* address) {};
    virtual void OnConsoleMessage(const char* message, const char* source, int lineNumber) { };
    virtual void OnTitleChange(const char* title) {};
    virtual void OnTargetUrlChange(const char* url) {};
    virtual void OnCursorChange(CursorType cursorType) {};

    virtual void OnLoadStart(const char* address) {};
    virtual void OnLoadEnd(const char* address) {};
    virtual void OnDocumentReady(const char* address) { loading = false; };

    virtual void OnCreateChildView(const char* sourceUrl, const char* targetUrl, bool isPopup) {};

    // The input and output JSValue instances should be arrays!!!
    virtual JSValue OnJavaScriptCall(const char* objName, const char* funcName, const JSValue& params) { return JSValue(); };

    bool loading;
};

void allocConsole()
{
    AllocConsole();

    freopen("CONIN$", "r", __acrt_iob_func(0));
    freopen("CONOUT$", "w", __acrt_iob_func(1));
    freopen("CONOUT$", "w", __acrt_iob_func(2));

    auto raw_output = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    auto raw_input = CreateFileA("CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);

    SetStdHandle(STD_INPUT_HANDLE, raw_input);
    SetStdHandle(STD_OUTPUT_HANDLE, raw_output);
    SetStdHandle(STD_ERROR_HANDLE, raw_output);
}

void getSystem()
{
    auto html_lib = LoadLibraryA("html_chromium.dll");

    if (!html_lib)
    {
        MessageBox(NULL, L"Failed to get html_chromium.dll (need Gmod x86-64)", L"Error", MB_ICONERROR);
        return;
    }

    g_HtmlSystem = *(IHtmlSystem**)GetProcAddress(html_lib, "g_pHtmlSystem");
}

void getGmodWindow()
{
    gmod_window = GetActiveWindow();
}

void getSize(int& width, int& height)
{
    RECT rect;

    GetClientRect(gmod_window, &rect);

    POINT topLeft = { rect.left, rect.top };
    POINT bottomRight = { rect.right, rect.bottom };

    ClientToScreen(gmod_window, &topLeft);
    ClientToScreen(gmod_window, &bottomRight);

    width = bottomRight.x - topLeft.x;
    height = bottomRight.y - topLeft.y;
}

const char* vertex_shader_src = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
out vec2 TexCoord;
void main()
{
    gl_Position = vec4(aPos.x, aPos.y, aPos.z, 1.0);
    TexCoord = aTexCoord;
})";

const char* fragment_shader_src = R"(
#version 330 core
out vec4 FragColor;
in vec2 TexCoord;
uniform sampler2D html_texture;
void main()
{
    FragColor = texture(html_texture, vec2(TexCoord.x, 1.0-TexCoord.y));
})";

bool gmod_in_focus = true;

IHtmlClient* current_client;
GmodOverlayClientListener* current_listener;

bool client_active = false;
volatile bool client_cleanup = false;

int current_width;
int current_height;

unsigned int current_texture;

uint8_t* pixel_data = nullptr;

void createTexture(int width, int height)
{
    if (current_texture)
        glDeleteTextures(1, &current_texture);

    if (pixel_data)
        delete[] pixel_data;

    pixel_data = new uint8_t[width * height * 4];

    memset(pixel_data, 0, width * height * 4);

    glGenTextures(1, &current_texture);
    glBindTexture(GL_TEXTURE_2D, current_texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, pixel_data);
}

int loading_box_width = 400, loading_box_height = 66;
int loading_box_offset_x = 10, loading_box_offset_y = 10;

void updateTexture(const unsigned char* data)
{
    if (!current_texture) return;

    memcpy(pixel_data, data, current_width * current_height * 4);

    int loading_box_pos_x = current_width - loading_box_offset_x - loading_box_width;
    int loading_box_pos_y = current_height - loading_box_offset_y - loading_box_height;

    for (int y = loading_box_pos_y; y < current_height - loading_box_offset_y; y++)
        for (int x = loading_box_pos_x; x < current_width - loading_box_offset_x; x++)
        {
            int pos = (x + y * current_width) * 4;

            // pixel_data[pos + 0];
            // pixel_data[pos + 1];
            // pixel_data[pos + 2];
            pixel_data[pos + 3] = 0x00; // set alpha to zero
        }

    glBindTexture(GL_TEXTURE_2D, current_texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, current_width, current_height, 0, GL_BGRA, GL_UNSIGNED_BYTE, pixel_data);
}

void createWindow()
{
    glfwInit();

    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    auto* window = glfwCreateWindow(512, 512, "Gmod Overlay", nullptr, nullptr);

    glfwMakeContextCurrent(window);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        return;
    }

    HWND hwnd = glfwGetWin32Window(window);

    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    exStyle |= WS_EX_LAYERED | WS_EX_TRANSPARENT;
    SetWindowLong(hwnd, GWL_EXSTYLE, exStyle);

    int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertex_shader_src, NULL);
    glCompileShader(vertexShader);

    int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragment_shader_src, NULL);
    glCompileShader(fragmentShader);

    int shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    float vertices[] = {
     1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
     1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
    -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
    -1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
    };

    unsigned int indices[] = {
        0, 1, 3,
        1, 2, 3
    };

    unsigned int VBO, VAO, EBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glBindVertexArray(0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    createTexture(current_width, current_height);

    while (client_active)
    {
        RECT rect;

        GetClientRect(gmod_window, &rect);

        POINT topLeft = { rect.left, rect.top };
        POINT bottomRight = { rect.right, rect.bottom };

        ClientToScreen(gmod_window, &topLeft);
        ClientToScreen(gmod_window, &bottomRight);

        int clientWidth = bottomRight.x - topLeft.x;
        int clientHeight = bottomRight.y - topLeft.y;

        glfwSetWindowPos(window, topLeft.x, topLeft.y);
        glfwSetWindowSize(window, clientWidth, clientHeight);

        glViewport(0, 0, clientWidth, clientHeight);

        glClearColor(0.f, 0.f, 0.f, 0.f);
        glClear(GL_COLOR_BUFFER_BIT);

        if (current_width != clientWidth || current_height != clientHeight)
        {
            createTexture(clientWidth, clientHeight);
            current_width = clientWidth;
            current_height = clientHeight;
        }

        if (current_client && current_client->LockImageData())
        {
            int html_width, html_height;
            auto data = current_client->GetImageData(html_width, html_height);
            
            updateTexture(data);

            current_client->UnlockImageData();
        }

        if (gmod_in_focus)
        {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, current_texture);
            glUseProgram(shaderProgram);
            glBindVertexArray(VAO);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &EBO);

    glDeleteProgram(shaderProgram);

    glDeleteTextures(1, &current_texture);

    delete[] pixel_data;
    pixel_data = nullptr;

    glfwDestroyWindow(window);
    glfwTerminate();

    client_cleanup = true;
}

LUA_FUNCTION(gmodOverlayIsActive)
{
    if (current_client)
        LUA->PushBool(true);
    else
        LUA->PushBool(false);

    return 1;
}

LUA_FUNCTION(gmodOverlayActivate)
{
    if (current_client) return 0;

    int width, height;

    getSize(width, height);

    current_width = width;
    current_height = height;

    current_listener = new GmodOverlayClientListener();
    current_client = g_HtmlSystem->CreateClient(current_listener);

    current_client->SetSize(width, height);

    client_active = true;
    client_cleanup = false;

    std::thread thrd(createWindow);
    thrd.detach();

    return 0;
}

LUA_FUNCTION(gmodOverlayShowURL)
{
    if (!current_client) return 0;

    const char* url = LUA->GetString(1);
    
    current_client->LoadUrl(url);

    return 0;
}

LUA_FUNCTION(gmodOverlayRunJavascript)
{
    if (!current_client) return 0;

    const char* js = LUA->GetString(1);

    current_client->RunJavaScript(js);

    return 0;
}

LUA_FUNCTION(gmodOverlayIsLoading)
{
    LUA->PushBool(current_listener->loading);

    return 0;
}

LUA_FUNCTION(gmodOverlayDeactivate)
{
    if (!current_client) return 0;

    client_active = false;

    while (!client_cleanup) {}

    current_client->Close();
    current_client = nullptr;

    client_cleanup = false;

    delete current_listener;

    return 0;
}

GMOD_MODULE_OPEN()
{
    getGmodWindow();
    getSystem();

    LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
        LUA->PushCFunction(gmodOverlayIsActive);
        LUA->SetField(-2, "gmodOverlayIsActive");
        LUA->PushCFunction(gmodOverlayActivate);
        LUA->SetField(-2, "gmodOverlayActivate");
        LUA->PushCFunction(gmodOverlayShowURL);
        LUA->SetField(-2, "gmodOverlayShowURL");
        LUA->PushCFunction(gmodOverlayRunJavascript);
        LUA->SetField(-2, "gmodOverlayRunJavascript");
        LUA->PushCFunction(gmodOverlayIsLoading);
        LUA->SetField(-2, "gmodOverlayIsLoading");
        LUA->PushCFunction(gmodOverlayDeactivate);
        LUA->SetField(-2, "gmodOverlayDeactivate");
    LUA->Pop();

    return 0;
}

GMOD_MODULE_CLOSE()
{
    return 0;
}
