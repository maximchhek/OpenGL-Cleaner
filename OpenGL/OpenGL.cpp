#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <sstream> 
#define STB_IMAGE_IMPLEMENTATION  
#include <stb_image.h>  


// Шейдеры

const char* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;       // Позиция вершины
layout (location = 1) in vec3 aNormal;    // Нормаль вершины
layout (location = 2) in vec2 aTexCoord;  // Текстурные координаты
uniform vec3 cursorWorldPos; 

out vec3 FragPos;       // Позиция фрагмента в мировом пространстве
out vec3 Normal;        // Нормаль фрагмента 
out vec2 TexCoord;      // Текстурные координаты

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    FragPos = vec3(model * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(model))) * aNormal;
    TexCoord = aTexCoord;

    gl_Position = projection * view * vec4(FragPos, 1.0);
}
)";

const char* fragmentShaderSource = R"(
#version 330 core

struct Ray {
    vec3 origin;
    vec3 dir;
};

struct HitInfo {
    float t;
    vec3 normal;
    bool hit;
};

HitInfo intersectFloor(Ray ray) {
    HitInfo hit;
    float t = (0.0 - ray.origin.y) / ray.dir.y;
    if (t > 0.001) {
        hit.t = t;
        hit.normal = vec3(0, 1, 0);
        hit.hit = true;
    } else {
        hit.hit = false;
    }
    return hit;
}

out vec4 FragColor;

in vec3 FragPos;       // Позиция фрагмента
in vec3 Normal;        // Нормаль фрагмента
in vec2 TexCoord;      // Текстурные координаты

uniform vec3 lightPos;     // Позиция источника света
uniform vec3 viewPos;      // Позиция камеры
uniform vec3 lightColor;   // Цвет света
uniform vec3 lightDir;     // Направление света
uniform float cutOff;      // Внутренний угол отсечения
uniform float outerCutOff; // Внешний угол отсечения 

uniform sampler2D texture1;  // Основная текстура
uniform samplerCube skybox;  // Карта отражений 
uniform bool isMirror;

#define NUM_LAMPS 3
uniform vec3 lampPositions[NUM_LAMPS];
uniform vec3 lampColors[NUM_LAMPS];
uniform bool isLamp;

void main() {
    if (isLamp) {
        FragColor = vec4(1.0);
        return;
    }

	if (isMirror) {
        Ray ray;
        ray.origin = FragPos + 0.001 * Normal;
        ray.dir = reflect(normalize(FragPos - viewPos), normalize(Normal));
        HitInfo hit = intersectFloor(ray);
        if (hit.hit) {
            vec3 hitPoint = ray.origin + ray.dir * hit.t;
            vec3 lightDirNorm = normalize(lightPos - hitPoint);
            float diff = max(dot(hit.normal, lightDirNorm), 0.0);
            vec3 color = vec3(0.7, 0.7, 0.7) * diff + 0.1;
            FragColor = vec4(color, 1.0);
        } else {
            FragColor = vec4(0.3, 0.5, 0.8, 1.0);
        }
        return;
    }
    

    // Освещение по Фонгу (основной прожектор)
    vec3 norm = normalize(Normal);
    vec3 lightDirection = normalize(lightPos - FragPos);

    // Диффузное освещение
    float diff = max(dot(norm, lightDirection), 0.0);

    // Зеркальное освещение
    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 reflectDir = reflect(-lightDirection, norm);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32);

    // Прожектор
    float theta = dot(normalize(lightDirection), normalize(-lightDir));
    float epsilon = cutOff - outerCutOff;
    float intensity = clamp((theta - outerCutOff) / epsilon, 0.0, 1.0);

    vec3 ambient = 0.1 * lightColor;
    vec3 diffuse = diff * lightColor * intensity;
    vec3 specular = spec * lightColor * intensity;

    //Освещение от настенных ламп
    vec3 lampDiffuse = vec3(0.0);
    vec3 lampSpecular = vec3(0.0);
    
    for(int i = 0; i < NUM_LAMPS; i++) {
        vec3 lampDir = normalize(lampPositions[i] - FragPos);
        float distance = length(lampPositions[i] - FragPos);
		float attenuation = 1.0 / (1.0 + 0.1 * distance + 0.05 * (distance * distance));
        
        float lampDiff = max(dot(norm, lampDir), 0.0);
        lampDiffuse += lampDiff * lampColors[i] * attenuation * 1.0;
        
        vec3 lampReflectDir = reflect(-lampDir, norm);
        float lampSpec = pow(max(dot(viewDir, lampReflectDir), 0.0), 32);
        lampSpecular += lampSpec * lampColors[i] * attenuation * 0.5;
    }

    vec3 phong = ambient + (diffuse + specular) * intensity + lampDiffuse + lampSpecular;

    // Карта отражений
    vec3 I = normalize(FragPos - viewPos);
    vec3 reflection = texture(skybox, reflect(I, norm)).rgb;

    // Итоговый цвет
    vec3 textureColor = texture(texture1, TexCoord).rgb;
    vec3 finalColor = mix(phong * textureColor, reflection * 1.5, 0.1);

    FragColor = vec4(finalColor, 1.0);

}
)";

const char* uiVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;

out vec3 Color;

uniform mat4 projection;
uniform mat4 model;

void main() {
    Color = aColor;
    gl_Position = projection * model * vec4(aPos, 1.0);
}
)";

const char* uiFragmentShaderSource = R"(
#version 330 core
in vec3 Color;
out vec4 FragColor;

void main() {
    FragColor = vec4(Color, 0.5);
}
)";

// Вершины для пола
float floorVertices[] = {
	// Позиции           // Нормали         // Текстуры
	-10.0f, 0.0f, -10.0f,  0.0f, 1.0f, 0.0f,  0.0f, 0.0f,
	10.0f, 0.0f, -10.0f,  0.0f, 1.0f, 0.0f,  1.0f, 0.0f,
	10.0f, 0.0f,  10.0f,  0.0f, 1.0f, 0.0f,  1.0f, 1.0f,
	-10.0f, 0.0f,  10.0f,  0.0f, 1.0f, 0.0f,  0.0f, 1.0f
};

unsigned int floorIndices[] = {
	0, 1, 2,
	2, 3, 0
};

float WallVertices[] = {
	// Старые координаты: 5.0f вызывали многократное повторение
	-10.0f, 0.0f, -10.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
	 10.0f, 0.0f, -10.0f, 0.0f, 1.0f, 0.0f, 2.0f, 0.0f,
	 10.0f, 5.0f, -10.0f, 0.0f, 1.0f, 0.0f, 2.0f, 1.0f, 
	-10.0f, 5.0f, -10.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f
};

unsigned int WallIndices[] = {
	0, 1, 2,
	2, 3, 0
};

std::vector<glm::vec3> lampPositions = {
	glm::vec3(-8.0f, 3.0f, -9.8f), 
	glm::vec3(0.0f, 3.0f, -9.8f),
	glm::vec3(8.0f, 3.0f, -9.8f)
};
float cubeVertices[] = {
	// Позиции          // Нормали
	// Задняя грань
	-0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
	 0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
	 0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
	-0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,

	// Передняя грань
	-0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
	 0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
	 0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
	-0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,

	// Левая грань
	-0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,
	-0.5f,  0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
	-0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
	-0.5f, -0.5f,  0.5f, -1.0f,  0.0f,  0.0f,

	// Правая грань
	 0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,
	 0.5f,  0.5f, -0.5f,  1.0f,  0.0f,  0.0f,
	 0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,
	 0.5f, -0.5f,  0.5f,  1.0f,  0.0f,  0.0f,

	 // Нижняя грань
	 -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,
	  0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,
	  0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,
	 -0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,

	 // Верхняя грань
	 -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,
	  0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,
	  0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,
	 -0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f
};


unsigned int cubeIndices[] = {
	0, 1, 2, 2, 3, 0,  // Задняя грань
	4, 5, 6, 6, 7, 4,  // Передняя грань
	8, 9, 10, 10, 11, 8,  // Левая грань
	12, 13, 14, 14, 15, 12,  // Правая грань
	16, 17, 18, 18, 19, 16,  // Нижняя грань
	20, 21, 22, 22, 23, 20   // Верхняя грань
};

float timerBarVertices[] = {
	// Позиции           // Цвета (R, G, B)
	-0.9f,  0.9f, 0.0f,  0.0f, 1.0f, 0.0f, // Левый верх
	 0.9f,  0.9f, 0.0f,  0.0f, 1.0f, 0.0f, // Правый верх
	 0.9f,  0.85f, 0.0f, 0.0f, 1.0f, 0.0f, // Правый низ
	-0.9f,  0.85f, 0.0f, 0.0f, 1.0f, 0.0f  // Левый низ
};

unsigned int timerBarIndices[] = {
	0, 1, 2,
	2, 3, 0
};

float mirrorVertices[] = {
	// Позиции           // Нормали         // Текстуры
	-2.0f, 1.0f, -9.99f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f,
	 2.0f, 1.0f, -9.99f,  0.0f, 0.0f, 1.0f,  1.0f, 0.0f,
	 2.0f, 3.0f, -9.99f,  0.0f, 0.0f, 1.0f,  1.0f, 1.0f,
	-2.0f, 3.0f, -9.99f,  0.0f, 0.0f, 1.0f,  0.0f, 1.0f
};
unsigned int mirrorIndices[] = {
	0, 1, 2,
	2, 3, 0
};

// Позиция робота-пылесоса
glm::vec3 robotPosition(0.0f, 0.5f, 0.0f);

// Камера
glm::vec3 cameraPosition(0.0f, 3.0f, 5.0f);
glm::vec3 cameraFront(0.0f, -0.5f, -1.0f);
glm::vec3 cameraUp(0.0f, 1.0f, 0.0f);

// Объекты для уборки
std::vector<glm::vec3> objects;
int score = 0;

// Таймер
float batteryLife = 100.0f; // Заряд батареи (в процентах)

// Генерация объектов
void generateObjects(int count) {
	srand(static_cast<unsigned int>(time(0)));
	for (int i = 0; i < count; ++i) {
		float x = static_cast<float>(rand() % 18 - 9);
		float z = static_cast<float>(rand() % 18 - 9);
		objects.push_back(glm::vec3(x, 0.2f, z));
	}
}

// Проверка столкновений
void checkCollisions() {
	for (auto it = objects.begin(); it != objects.end();) {
		if (glm::distance(robotPosition, *it) < 0.6f) {
			it = objects.erase(it);
			score++;
		}
		else {
			++it;
		}
	}
}

glm::vec3 robotDirection(0.0f, 0.0f, -1.0f); 
const float robotSpeed = 0.05f; 

// Обработка ввода
void processInput(GLFWwindow* window) {
	const float rotationSpeed = glm::radians(1.0f);

	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
		float angle = rotationSpeed;
		glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 1.0f, 0.0f));
		robotDirection = glm::vec3(rotation * glm::vec4(robotDirection, 0.0f));
	}

	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
		float angle = -rotationSpeed;
		glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 1.0f, 0.0f));
		robotDirection = glm::vec3(rotation * glm::vec4(robotDirection, 0.0f));
	}

	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
		robotPosition += robotDirection * robotSpeed;
	}

	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
		robotPosition -= robotDirection * robotSpeed;
	}

	if (robotPosition.x < -9.0f) robotPosition.x = -9.0f;
	if (robotPosition.x > 9.0f) robotPosition.x = 9.0f;
	if (robotPosition.z < -9.0f) robotPosition.z = -9.0f;
	if (robotPosition.z > 9.0f) robotPosition.z = 9.0f;

	checkCollisions();
}



// Отображение текста (счетчика)
void renderText(GLFWwindow* window, const std::string& text) {
	glfwSetWindowTitle(window, text.c_str());
}


// Проверка ошибок компиляции шейдеров
void checkShaderCompilation(unsigned int shader, const std::string& type) {
	int success;
	char infoLog[1024];
	if (type == "PROGRAM") {
		glGetProgramiv(shader, GL_LINK_STATUS, &success);
		if (!success) {
			glGetProgramInfoLog(shader, 1024, NULL, infoLog);
			std::cerr << "ERROR::PROGRAM_LINKING_ERROR of type: " << type << "\n" << infoLog << "\n";
		}
	}
	else {
		glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
		if (!success) {
			glGetShaderInfoLog(shader, 1024, NULL, infoLog);
			std::cerr << "ERROR::SHADER_COMPILATION_ERROR of type: " << type << "\n" << infoLog << "\n";
		}
	}
}

// Глобальная переменная для текстуры пола
unsigned int floorTexture;
unsigned int wallTexture;

// Функция для загрузки текстуры
unsigned int loadTexture(const char* path) {
	unsigned int textureID;
	glGenTextures(1, &textureID);
	glBindTexture(GL_TEXTURE_2D, textureID);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	// Загрузка изображения с помощью stb_image
	int width, height, nrChannels;
	stbi_set_flip_vertically_on_load(true); 
	unsigned char* data = stbi_load(path, &width, &height, &nrChannels, 0);

	if (data) {
		GLenum format = GL_RGB;
		if (nrChannels == 1)
			format = GL_RED;
		else if (nrChannels == 3)
			format = GL_RGB;
		else if (nrChannels == 4)
			format = GL_RGBA;

		glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
		glGenerateMipmap(GL_TEXTURE_2D);
	}
	else {
		std::cerr << "Failed to load texture: " << path << std::endl;
	}

	stbi_image_free(data);

	return textureID;
}

// Рендер пола
void renderFloor(unsigned int shaderProgram, unsigned int floorVAO) {
	glUseProgram(shaderProgram);

	glm::mat4 model = glm::mat4(1.0f);
	unsigned int modelLoc = glGetUniformLocation(shaderProgram, "model");
	glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));

	glBindVertexArray(floorVAO);
	glBindTexture(GL_TEXTURE_2D, floorTexture);
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
}

// Рендер стены
void renderWall(unsigned int shaderProgram, unsigned int WallVAO) {
	glUseProgram(shaderProgram);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, wallTexture);
	glUniform1i(glGetUniformLocation(shaderProgram, "texture1"), 0);

	glm::mat4 model = glm::mat4(1.0f);
	unsigned int modelLoc = glGetUniformLocation(shaderProgram, "model");
	glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));

	glBindVertexArray(WallVAO);
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
}

// Рендер робота
void renderRobot(unsigned int shaderProgram, unsigned int cubeVAO) {
	glUseProgram(shaderProgram);

	glm::mat4 model = glm::translate(glm::mat4(1.0f), robotPosition);
	float angle = glm::atan(robotDirection.x, robotDirection.z); 
	model = glm::rotate(model, angle, glm::vec3(0.0f, 1.0f, 0.0f)); 

	unsigned int modelLoc = glGetUniformLocation(shaderProgram, "model");
	glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));

	glBindVertexArray(cubeVAO);
	glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
}

//Рендер объектов
void renderObjects(unsigned int shaderProgram, unsigned int cubeVAO, const std::vector<glm::vec3>& objects) {
	glUseProgram(shaderProgram);
	float scaleFactor = 0.7f; 

	for (const auto& obj : objects) {
		glm::mat4 model = glm::translate(glm::mat4(1.0f), obj);
		model = glm::scale(model, glm::vec3(scaleFactor)); 
		unsigned int modelLoc = glGetUniformLocation(shaderProgram, "model");
		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));

		glBindVertexArray(cubeVAO);
		glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
	}
}

void renderGameOverText(unsigned int shaderProgram, const std::string& message, const glm::mat4& orthoProjection) {
	glUseProgram(shaderProgram);

	unsigned int projLoc = glGetUniformLocation(shaderProgram, "projection");
	glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(orthoProjection));

	glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f));
	unsigned int modelLoc = glGetUniformLocation(shaderProgram, "model");
	glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));

	unsigned int viewLoc = glGetUniformLocation(shaderProgram, "view");
	glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(glm::mat4(1.0f)));


	std::cout << message << std::endl;
}

unsigned int loadCubemap(std::vector<std::string> faces) {
	unsigned int textureID;
	glGenTextures(1, &textureID);
	glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);

	int width, height, nrChannels;
	for (unsigned int i = 0; i < faces.size(); i++) {
		unsigned char* data = stbi_load(faces[i].c_str(), &width, &height, &nrChannels, 0);
		if (data) {
			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
			stbi_image_free(data);
		}
		else {
			std::cerr << "Cubemap texture failed to load at path: " << faces[i] << std::endl;
			stbi_image_free(data);
		}
	}
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

	return textureID;
}

void renderTimerBar(unsigned int uiShaderProgram, unsigned int timerBarVAO, float batteryLife, const glm::mat4& orthoProjection) {
	glUseProgram(uiShaderProgram);

	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glUniformMatrix4fv(glGetUniformLocation(uiShaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(orthoProjection));

	glm::mat4 model = glm::scale(glm::mat4(1.0f), glm::vec3(batteryLife / 100.0f, 1.0f, 1.0f));
	glUniformMatrix4fv(glGetUniformLocation(uiShaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));

	glBindVertexArray(timerBarVAO);
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
}

void renderMirror(unsigned int shaderProgram, unsigned int mirrorVAO) {
	glUseProgram(shaderProgram);
	glm::mat4 model = glm::mat4(1.0f);
	unsigned int modelLoc = glGetUniformLocation(shaderProgram, "model");
	glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
	glBindVertexArray(mirrorVAO);
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
}

double cursorX = 0.0, cursorY = 0.0;

void cursorPositionCallback(GLFWwindow* window, double xpos, double ypos) {
	cursorX = xpos;
	cursorY = ypos;
}

int main() {
	bool gameOver = false;
	if (!glfwInit()) return -1;

	GLFWwindow* window = glfwCreateWindow(1080, 720, "Vacuum Cleaner Simulator", nullptr, nullptr);
	glfwSetCursorPosCallback(window, cursorPositionCallback);
	if (!window) {
		glfwTerminate();
		return -1;
	}

	glfwMakeContextCurrent(window);
	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
		std::cerr << "Failed to initialize GLAD" << std::endl;
		return -1;
	}

	// Компиляция шейдеров
	unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertexShader, 1, &vertexShaderSource, nullptr);
	glCompileShader(vertexShader);

	unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragmentShader, 1, &fragmentShaderSource, nullptr);
	glCompileShader(fragmentShader);

	unsigned int shaderProgram = glCreateProgram();
	glAttachShader(shaderProgram, vertexShader);
	glAttachShader(shaderProgram, fragmentShader);
	glLinkProgram(shaderProgram);

	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);

	// Настройка буферов для пола
	unsigned int floorVAO, floorVBO, floorEBO;
	glGenVertexArrays(1, &floorVAO);
	glGenBuffers(1, &floorVBO);
	glGenBuffers(1, &floorEBO);

	glBindVertexArray(floorVAO);

	glBindBuffer(GL_ARRAY_BUFFER, floorVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(floorVertices), floorVertices, GL_STATIC_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, floorEBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(floorIndices), floorIndices, GL_STATIC_DRAW);

	// Позиции
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);

	// Нормали
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
	glEnableVertexAttribArray(1);

	// Текстура
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
	glEnableVertexAttribArray(2);

	// Настройка буферов для стен
	unsigned int WallVAO, WallVBO, WallEBO;
	glGenVertexArrays(1, &WallVAO);
	glGenBuffers(1, &WallVBO);
	glGenBuffers(1, &WallEBO);

	glBindVertexArray(WallVAO);

	glBindBuffer(GL_ARRAY_BUFFER, WallVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(WallVertices), WallVertices, GL_STATIC_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, WallEBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(WallIndices), WallIndices, GL_STATIC_DRAW);

	// Настройка атрибутов для стен
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);

	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
	glEnableVertexAttribArray(1);

	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
	glEnableVertexAttribArray(2);

	// Настройка буферов для куба
	unsigned int cubeVAO, cubeVBO, cubeEBO;
	glGenVertexArrays(1, &cubeVAO);
	glGenBuffers(1, &cubeVBO);
	glGenBuffers(1, &cubeEBO);

	glBindVertexArray(cubeVAO);

	glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), cubeVertices, GL_STATIC_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cubeEBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(cubeIndices), cubeIndices, GL_STATIC_DRAW);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);

	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float))); 
	glEnableVertexAttribArray(1);

	glEnable(GL_DEPTH_TEST);

	// Настройка буферов для полоски таймера
	unsigned int timerBarVAO, timerBarVBO, timerBarEBO;
	glGenVertexArrays(1, &timerBarVAO);
	glGenBuffers(1, &timerBarVBO);
	glGenBuffers(1, &timerBarEBO);

	glBindVertexArray(timerBarVAO);

	glBindBuffer(GL_ARRAY_BUFFER, timerBarVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(timerBarVertices), timerBarVertices, GL_STATIC_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, timerBarEBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(timerBarIndices), timerBarIndices, GL_STATIC_DRAW);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);

	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
	glEnableVertexAttribArray(1);

	// Создаем UI шейдерную программу для таймбара
	unsigned int uiVertexShader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(uiVertexShader, 1, &uiVertexShaderSource, NULL);
	glCompileShader(uiVertexShader);

	unsigned int uiFragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(uiFragmentShader, 1, &uiFragmentShaderSource, NULL);
	glCompileShader(uiFragmentShader);

	unsigned int uiShaderProgram = glCreateProgram();
	glAttachShader(uiShaderProgram, uiVertexShader);
	glAttachShader(uiShaderProgram, uiFragmentShader);
	glLinkProgram(uiShaderProgram);

	// Удаляем шейдеры после линковки
	glDeleteShader(uiVertexShader);
	glDeleteShader(uiFragmentShader);

	// Настройка буферов для зеркала
	unsigned int mirrorVAO, mirrorVBO, mirrorEBO;
	glGenVertexArrays(1, &mirrorVAO);
	glGenBuffers(1, &mirrorVBO);
	glGenBuffers(1, &mirrorEBO);

	glBindVertexArray(mirrorVAO);

	glBindBuffer(GL_ARRAY_BUFFER, mirrorVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(mirrorVertices), mirrorVertices, GL_STATIC_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mirrorEBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(mirrorIndices), mirrorIndices, GL_STATIC_DRAW);

	// Позиции
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);
	// Нормали
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
	glEnableVertexAttribArray(1);
	// Текстуры
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
	glEnableVertexAttribArray(2);


	// Генерация объектов
	generateObjects(20);
	floorTexture = loadTexture("floor-texture.jpg");
	wallTexture = loadTexture("wall-texture.jpg");

	while (!glfwWindowShouldClose(window)) {
		if (!gameOver) {

			processInput(window);

			glm::vec3 newPosition = robotPosition + robotDirection * robotSpeed;

			if (newPosition.x > -9.5f && newPosition.x < 9.5f && newPosition.z > -9.5f && newPosition.z < 9.5f) {
				robotPosition = newPosition;
			}

			// Обновление позиции камеры
			float cameraDistance = 5.0f;
			float cameraHeight = 10.0f; 
			cameraPosition = robotPosition - robotDirection * cameraDistance + glm::vec3(0.0f, cameraHeight, 0.0f);

			// Обновление направления взгляда камеры
			cameraFront = glm::normalize(robotPosition - cameraPosition);

			// Создание матрицы вида
			glm::mat4 view = glm::lookAt(cameraPosition, robotPosition, cameraUp);


			checkCollisions();

			// Уменьшение заряда батареи
			batteryLife -= 0.05f;
			if (batteryLife <= 0.0f) {
				gameOver = true;
				renderText(window, "Пылесос разрядился!");
			}

			// Проверка завершения игры
			if (objects.empty()) {
				gameOver = true;
				renderText(window, "Ура, ты все собрал!");
			}

			// Очистка экрана
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

			glUseProgram(shaderProgram);

			// Матрицы камеры
			glm::mat4 projection = glm::perspective(glm::radians(45.0f), 800.0f / 600.0f, 0.1f, 100.0f);

			unsigned int viewLoc = glGetUniformLocation(shaderProgram, "view");
			unsigned int projLoc = glGetUniformLocation(shaderProgram, "projection");

			glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
			glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));


			// Направление прожектора — по направлению робота
			glm::vec3 lightDir = glm::normalize(robotDirection);
			glUniform3f(glGetUniformLocation(shaderProgram, "lightDir"), lightDir.x, lightDir.y, lightDir.z);

			// Позиция света чуть спереди робота
			glm::vec3 lightPos = robotPosition + lightDir * 0.5f;
			glUniform3f(glGetUniformLocation(shaderProgram, "lightPos"), lightPos.x, lightPos.y, lightPos.z);
			float cutOff = glm::cos(glm::radians(55.0f));
			float outerCutOff = glm::cos(glm::radians(70.0f)); 

			unsigned int cutOffLoc = glGetUniformLocation(shaderProgram, "cutOff");
			glUniform1f(cutOffLoc, cutOff);

			unsigned int outerCutOffLoc = glGetUniformLocation(shaderProgram, "outerCutOff");
			glUniform1f(outerCutOffLoc, outerCutOff);


			unsigned int viewPosLoc = glGetUniformLocation(shaderProgram, "viewPos");
			glUniform3f(viewPosLoc, cameraPosition.x, cameraPosition.y, cameraPosition.z);

			unsigned int lightColorLoc = glGetUniformLocation(shaderProgram, "lightColor");
			glUniform3f(lightColorLoc, 1.0f, 1.0f, 1.0f);

			// Рендер пола
			renderFloor(shaderProgram, floorVAO);

			// Рендер стены
			renderWall(shaderProgram, WallVAO);

			// Рендер зеркала
			glUniform1i(glGetUniformLocation(shaderProgram, "isMirror"), 1);
			renderMirror(shaderProgram, mirrorVAO);
			glUniform1i(glGetUniformLocation(shaderProgram, "isMirror"), 0);

			// Рендер робота-пылесоса
			renderRobot(shaderProgram, cubeVAO);

			// Рендер объектов
			renderObjects(shaderProgram, cubeVAO, objects);

			// Ортографическая проекция для UI
			glm::mat4 orthoProjection = glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f);

			// Рендер лампочек
			glUniform1i(glGetUniformLocation(shaderProgram, "isLamp"), 1);
			for (const auto& pos : lampPositions) {
				glm::mat4 model = glm::translate(glm::mat4(1.0f), pos);
				model = glm::scale(model, glm::vec3(0.2f)); 
				glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
				glBindVertexArray(cubeVAO);
				glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
			}
			glUniform1i(glGetUniformLocation(shaderProgram, "isLamp"), 0);

			// Рендер полоски таймера
			renderTimerBar(uiShaderProgram, timerBarVAO, batteryLife, orthoProjection);


			glUseProgram(shaderProgram);
			for (int i = 0; i < lampPositions.size(); ++i) {
				std::string posName = "lampPositions[" + std::to_string(i) + "]";
				glUniform3f(glGetUniformLocation(shaderProgram, posName.c_str()),
					lampPositions[i].x, lampPositions[i].y, lampPositions[i].z);

				std::string colorName = "lampColors[" + std::to_string(i) + "]";
				glUniform3f(glGetUniformLocation(shaderProgram, colorName.c_str()), 0.8f, 0.7f, 0.6f);
			}

			glfwSwapBuffers(window);
			glfwPollEvents();
		}
		else {
			// Очистка экрана
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

			// Ортографическая проекция для UI
			glm::mat4 orthoProjection = glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f);

			// Рендер сообщения о завершении игры
			renderGameOverText(shaderProgram, "Нажмите R, чтобы сыграть снова", orthoProjection);

			glfwSwapBuffers(window);
			glfwPollEvents();

			// Проверка нажатия клавиши R для перезапуска
			if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS) {
				gameOver = false;
				batteryLife = 100.0f;
				score = 0;
				objects.clear();
				generateObjects(20);
			}
		}
	}

	glDeleteVertexArrays(1, &floorVAO);
	glDeleteBuffers(1, &floorVBO);
	glDeleteBuffers(1, &floorEBO);

	glDeleteVertexArrays(1, &cubeVAO);
	glDeleteBuffers(1, &cubeVBO);
	glDeleteBuffers(1, &cubeEBO);

	glfwTerminate();
	return 0;
}