#pragma once

#include <nlohmann/json.hpp>
#include <QString>

void Init_OpenGL();
void Read_Json(const QString &path, nlohmann::json &json);
QString App_File(const QString &relativePath); //相对 exe 所在目录解析文件路径


const char *Vertex_Shader();
const char *Fragment_Shader();
const float *Vertices(); //桌宠视图顶点数据