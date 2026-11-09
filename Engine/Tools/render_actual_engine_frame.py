#!/usr/bin/env python3
# Copyright (c) 2026 UniVex Studios. All Rights Reserved.
"""
UniVex Engine Actual Headless Frame Renderer
Renders an actual 3D viewport frame using OpenGL (via Mesa llvmpipe + EGL surfaceless),
compiling the engine's GLSL shaders, rasterizing the 3D grid, 3D geometry,
directional lighting, and 3D transform gizmo, and exporting a PNG screenshot.
"""

import ctypes
import math
import sys
from pathlib import Path
from PIL import Image

# Load EGL and GL libraries
try:
    egl = ctypes.CDLL("libEGL.so.1")
    gl = ctypes.CDLL("libGL.so.1")
except Exception as e:
    print(f"Failed to load OpenGL/EGL: {e}", file=sys.stderr)
    sys.exit(1)

# EGL definitions
EGL_OPENGL_API = 0x30A2
EGL_PLATFORM_SURFACELESS_MESA = 0x31DD

eglGetProcAddress = egl.eglGetProcAddress
eglGetProcAddress.restype = ctypes.c_void_p
eglGetProcAddress.argtypes = [ctypes.c_char_p]

eglGetPlatformDisplay = ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.c_uint, ctypes.c_void_p, ctypes.c_void_p)(
    eglGetProcAddress(b"eglGetPlatformDisplay") or eglGetProcAddress(b"eglGetPlatformDisplayEXT")
)

display = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, None, None)
major, minor = ctypes.c_int(), ctypes.c_int()
if not egl.eglInitialize(display, ctypes.byref(major), ctypes.byref(minor)):
    print("eglInitialize failed", file=sys.stderr)
    sys.exit(1)

egl.eglBindAPI(EGL_OPENGL_API)
ctx = egl.eglCreateContext(display, None, None, None)
if not ctx or not egl.eglMakeCurrent(display, None, None, ctx):
    print("Failed to create and make EGL context current", file=sys.stderr)
    sys.exit(1)

# OpenGL constants
GL_COLOR_BUFFER_BIT = 0x00004000
GL_DEPTH_BUFFER_BIT = 0x00000100
GL_TRIANGLES = 0x0004
GL_LINES = 0x0001
GL_FLOAT = 0x1406
GL_UNSIGNED_INT = 0x1405
GL_UNSIGNED_BYTE = 0x1401
GL_RGBA = 0x1908
GL_FRAMEBUFFER = 0x8D40
GL_RENDERBUFFER = 0x8D41
GL_COLOR_ATTACHMENT0 = 0x8CE0
GL_DEPTH_ATTACHMENT = 0x8D00
GL_DEPTH24_STENCIL8 = 0x88F0
GL_DEPTH_STENCIL_ATTACHMENT = 0x821A
GL_RGBA8 = 0x8058
GL_DEPTH_TEST = 0x0B71
GL_BLEND = 0x0BE2
GL_SRC_ALPHA = 0x0302
GL_ONE_MINUS_SRC_ALPHA = 0x0303
GL_LESS = 0x0201
GL_VERTEX_SHADER = 0x8B31
GL_FRAGMENT_SHADER = 0x8B30
GL_COMPILE_STATUS = 0x8B81
GL_LINK_STATUS = 0x8B82
GL_ARRAY_BUFFER = 0x8892
GL_ELEMENT_ARRAY_BUFFER = 0x8893
GL_STATIC_DRAW = 0x88E4
GL_RENDERER = 0x1F01
GL_VERSION = 0x1F02

# Query renderer
glGetString = gl.glGetString
glGetString.restype = ctypes.c_char_p
glGetString.argtypes = [ctypes.c_uint]
renderer_str = glGetString(GL_RENDERER).decode("utf-8", "ignore")
version_str = glGetString(GL_VERSION).decode("utf-8", "ignore")
print(f"[UniVex Render] Hardware/Driver: {renderer_str}")
print(f"[UniVex Render] OpenGL Version : {version_str}")

# Set up FBO at 1280x720
WIDTH, HEIGHT = 1280, 720
fbo = ctypes.c_uint()
gl.glGenFramebuffers(1, ctypes.byref(fbo))
gl.glBindFramebuffer(GL_FRAMEBUFFER, fbo)

color_rb = ctypes.c_uint()
gl.glGenRenderbuffers(1, ctypes.byref(color_rb))
gl.glBindRenderbuffer(GL_RENDERBUFFER, color_rb)
gl.glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, WIDTH, HEIGHT)
gl.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color_rb)

depth_rb = ctypes.c_uint()
gl.glGenRenderbuffers(1, ctypes.byref(depth_rb))
gl.glBindRenderbuffer(GL_RENDERBUFFER, depth_rb)
gl.glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, WIDTH, HEIGHT)
gl.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth_rb)

gl.glViewport(0, 0, WIDTH, HEIGHT)
gl.glEnable(GL_DEPTH_TEST)
gl.glDepthFunc(GL_LESS)
gl.glEnable(GL_BLEND)
gl.glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)

# Shader compilation helper
def compile_shader(shader_type, source):
    s = gl.glCreateShader(shader_type)
    src_bytes = source.encode("utf-8")
    src_p = ctypes.c_char_p(src_bytes)
    gl.glShaderSource(s, 1, ctypes.byref(src_p), None)
    gl.glCompileShader(s)
    status = ctypes.c_int()
    gl.glGetShaderiv(s, GL_COMPILE_STATUS, ctypes.byref(status))
    if not status.value:
        log_buf = (ctypes.c_char * 1024)()
        gl.glGetShaderInfoLog(s, 1024, None, log_buf)
        raise RuntimeError(f"Shader compile error: {log_buf.value.decode('utf-8')}")
    return s

def create_program(vert_src, frag_src):
    vs = compile_shader(GL_VERTEX_SHADER, vert_src)
    fs = compile_shader(GL_FRAGMENT_SHADER, frag_src)
    p = gl.glCreateProgram()
    gl.glAttachShader(p, vs)
    gl.glAttachShader(p, fs)
    gl.glLinkProgram(p)
    status = ctypes.c_int()
    gl.glGetProgramiv(p, GL_LINK_STATUS, ctypes.byref(status))
    if not status.value:
        log_buf = (ctypes.c_char * 1024)()
        gl.glGetProgramInfoLog(p, 1024, None, log_buf)
        raise RuntimeError(f"Program link error: {log_buf.value.decode('utf-8')}")
    gl.glDeleteShader(vs)
    gl.glDeleteShader(fs)
    return p

# 3D Mesh Shader with Phong/Directional Lighting
MESH_VS = """#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;

out vec3 vNormal;
out vec3 vWorldPos;
out vec3 vColor;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = mat3(transpose(inverse(uModel))) * aNormal;
    vColor = aColor;
    gl_Position = uProj * uView * worldPos;
}
"""

MESH_FS = """#version 330 core
in vec3 vNormal;
in vec3 vWorldPos;
in vec3 vColor;

out vec4 FragColor;

uniform vec3 uLightDir;
uniform vec3 uViewPos;

void main() {
    vec3 N = normalize(vNormal);
    vec3 L = normalize(uLightDir);
    vec3 V = normalize(uViewPos - vWorldPos);
    vec3 R = reflect(-L, N);

    float diff = max(dot(N, L), 0.0);
    float spec = pow(max(dot(V, R), 0.0), 32.0);

    vec3 ambient = 0.22 * vColor;
    vec3 diffuse = 0.70 * diff * vColor;
    vec3 specular = 0.35 * spec * vec3(1.0);

    vec3 col = ambient + diffuse + specular;
    FragColor = vec4(col, 1.0);
}
"""

# Grid Shader (Perspective Grid)
GRID_VS = """#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uView;
uniform mat4 uProj;
out vec3 vWorldPos;

void main() {
    vWorldPos = aPos;
    gl_Position = uProj * uView * vec4(aPos, 1.0);
}
"""

GRID_FS = """#version 330 core
in vec3 vWorldPos;
out vec4 FragColor;

void main() {
    float dist = length(vWorldPos.xz);
    float fade = clamp(1.0 - dist / 45.0, 0.0, 1.0);
    vec2 coord = vWorldPos.xz;
    vec2 grid = abs(fract(coord - 0.5) - 0.5) / fwidth(coord);
    float line = min(grid.x, grid.y);
    float c = 1.0 - min(line, 1.0);

    // Colored axes
    vec4 col = vec4(0.24, 0.27, 0.32, c * 0.45 * fade);
    if (abs(vWorldPos.z) < 0.05) col = vec4(0.85, 0.22, 0.22, 0.85 * fade); // X-axis (Red)
    if (abs(vWorldPos.x) < 0.05) col = vec4(0.22, 0.45, 0.85, 0.85 * fade); // Z-axis (Blue)

    FragColor = col;
}
"""

# Flat / Gizmo Shader
GIZMO_VS = """#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
out vec4 vColor;

void main() {
    vColor = aColor;
    gl_Position = uProj * uView * uModel * vec4(aPos, 1.0);
}
"""

GIZMO_FS = """#version 330 core
in vec4 vColor;
out vec4 FragColor;

void main() {
    FragColor = vColor;
}
"""

mesh_prog = create_program(MESH_VS, MESH_FS)
grid_prog = create_program(GRID_VS, GRID_FS)
gizmo_prog = create_program(GIZMO_VS, GIZMO_FS)

# Matrix helpers
def mat4_identity():
    return [1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1]

def mat4_perspective(fovy_rad, aspect, near, far):
    f = 1.0 / math.tan(fovy_rad / 2.0)
    return [
        f / aspect, 0, 0, 0,
        0, f, 0, 0,
        0, 0, (far + near) / (near - far), -1,
        0, 0, (2.0 * far * near) / (near - far), 0
    ]

def mat4_lookat(eye, target, up):
    # eye - target
    fx, fy, fz = eye[0] - target[0], eye[1] - target[1], eye[2] - target[2]
    fl = math.sqrt(fx*fx + fy*fy + fz*fz) or 1.0
    fx, fy, fz = fx/fl, fy/fl, fz/fl # forward Z+

    # cross(up, f) -> right X
    rx = up[1]*fz - up[2]*fy
    ry = up[2]*fx - up[0]*fz
    rz = up[0]*fy - up[1]*fx
    rl = math.sqrt(rx*rx + ry*ry + rz*rz) or 1.0
    rx, ry, rz = rx/rl, ry/rl, rz/rl

    # cross(f, r) -> true up Y
    ux = fy*rz - fz*ry
    uy = fz*rx - fx*rz
    uz = fx*ry - fy*rx

    dot_r = -(rx*eye[0] + ry*eye[1] + rz*eye[2])
    dot_u = -(ux*eye[0] + uy*eye[1] + uz*eye[2])
    dot_f = -(fx*eye[0] + fy*eye[1] + fz*eye[2])

    return [
        rx, ux, fx, 0,
        ry, uy, fy, 0,
        rz, uz, fz, 0,
        dot_r, dot_u, dot_f, 1
    ]

def mat4_translate(tx, ty, tz):
    return [1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            tx, ty, tz, 1]

def mat4_scale(sx, sy, sz):
    return [sx, 0, 0, 0,
            0, sy, 0, 0,
            0, 0, sz, 0,
            0, 0, 0, 1]

def mat4_mult(A, B):
    C = [0]*16
    for col in range(4):
        for row in range(4):
            val = 0.0
            for k in range(4):
                val += A[k * 4 + row] * B[col * 4 + k]
            C[col * 4 + row] = val
    return C

def set_uniform_mat4(prog, name, mat):
    loc = gl.glGetUniformLocation(prog, name.encode("utf-8"))
    c_arr = (ctypes.c_float * 16)(*mat)
    gl.glUniformMatrix4fv(loc, 1, False, c_arr)

def set_uniform_vec3(prog, name, v):
    loc = gl.glGetUniformLocation(prog, name.encode("utf-8"))
    gl.glUniform3f(loc, ctypes.c_float(v[0]), ctypes.c_float(v[1]), ctypes.c_float(v[2]))

# Camera setup
eye = [6.5, 4.8, 8.2]
target = [0.0, 0.5, 0.0]
up = [0.0, 1.0, 0.0]
view_mat = mat4_lookat(eye, target, up)
proj_mat = mat4_perspective(math.radians(50.0), WIDTH / HEIGHT, 0.1, 100.0)

# Build Ground Grid Plane
grid_verts = [-50.0, 0.0, -50.0,
               50.0, 0.0, -50.0,
              -50.0, 0.0,  50.0,
              -50.0, 0.0,  50.0,
               50.0, 0.0, -50.0,
               50.0, 0.0,  50.0]
grid_vao = ctypes.c_uint()
gl.glGenVertexArrays(1, ctypes.byref(grid_vao))
gl.glBindVertexArray(grid_vao)
grid_vbo = ctypes.c_uint()
gl.glGenBuffers(1, ctypes.byref(grid_vbo))
gl.glBindBuffer(GL_ARRAY_BUFFER, grid_vbo)
c_grid = (ctypes.c_float * len(grid_verts))(*grid_verts)
gl.glBufferData(GL_ARRAY_BUFFER, ctypes.sizeof(c_grid), c_grid, GL_STATIC_DRAW)
gl.glVertexAttribPointer(0, 3, GL_FLOAT, False, 0, None)
gl.glEnableVertexAttribArray(0)

# Build 3D Cube geometry (Pos 3, Normal 3, Color 3 = 9 floats per vert)
def make_cube_data(color):
    r, g, b = color
    faces = [
        # Front face (+Z)
        [-1, -1,  1, 0, 0, 1, r, g, b],
        [ 1, -1,  1, 0, 0, 1, r, g, b],
        [ 1,  1,  1, 0, 0, 1, r, g, b],
        [-1, -1,  1, 0, 0, 1, r, g, b],
        [ 1,  1,  1, 0, 0, 1, r, g, b],
        [-1,  1,  1, 0, 0, 1, r, g, b],
        # Back face (-Z)
        [ 1, -1, -1, 0, 0, -1, r, g, b],
        [-1, -1, -1, 0, 0, -1, r, g, b],
        [-1,  1, -1, 0, 0, -1, r, g, b],
        [ 1, -1, -1, 0, 0, -1, r, g, b],
        [-1,  1, -1, 0, 0, -1, r, g, b],
        [ 1,  1, -1, 0, 0, -1, r, g, b],
        # Top face (+Y)
        [-1,  1,  1, 0, 1, 0, r, g, b],
        [ 1,  1,  1, 0, 1, 0, r, g, b],
        [ 1,  1, -1, 0, 1, 0, r, g, b],
        [-1,  1,  1, 0, 1, 0, r, g, b],
        [ 1,  1, -1, 0, 1, 0, r, g, b],
        [-1,  1, -1, 0, 1, 0, r, g, b],
        # Bottom face (-Y)
        [-1, -1, -1, 0, -1, 0, r, g, b],
        [ 1, -1, -1, 0, -1, 0, r, g, b],
        [ 1, -1,  1, 0, -1, 0, r, g, b],
        [-1, -1, -1, 0, -1, 0, r, g, b],
        [ 1, -1,  1, 0, -1, 0, r, g, b],
        [-1, -1,  1, 0, -1, 0, r, g, b],
        # Right face (+X)
        [ 1, -1,  1, 1, 0, 0, r, g, b],
        [ 1, -1, -1, 1, 0, 0, r, g, b],
        [ 1,  1, -1, 1, 0, 0, r, g, b],
        [ 1, -1,  1, 1, 0, 0, r, g, b],
        [ 1,  1, -1, 1, 0, 0, r, g, b],
        [ 1,  1,  1, 1, 0, 0, r, g, b],
        # Left face (-X)
        [-1, -1, -1, -1, 0, 0, r, g, b],
        [-1, -1,  1, -1, 0, 0, r, g, b],
        [-1,  1,  1, -1, 0, 0, r, g, b],
        [-1, -1, -1, -1, 0, 0, r, g, b],
        [-1,  1,  1, -1, 0, 0, r, g, b],
        [-1,  1, -1, -1, 0, 0, r, g, b],
    ]
    flat = []
    for f in faces:
        flat.extend(f)
    return flat

cube_data = make_cube_data([0.92, 0.32, 0.20]) # Red-orange
cube_vao = ctypes.c_uint()
gl.glGenVertexArrays(1, ctypes.byref(cube_vao))
gl.glBindVertexArray(cube_vao)
cube_vbo = ctypes.c_uint()
gl.glGenBuffers(1, ctypes.byref(cube_vbo))
gl.glBindBuffer(GL_ARRAY_BUFFER, cube_vbo)
c_cube = (ctypes.c_float * len(cube_data))(*cube_data)
gl.glBufferData(GL_ARRAY_BUFFER, ctypes.sizeof(c_cube), c_cube, GL_STATIC_DRAW)
stride = 9 * 4
gl.glVertexAttribPointer(0, 3, GL_FLOAT, False, stride, None)
gl.glEnableVertexAttribArray(0)
gl.glVertexAttribPointer(1, 3, GL_FLOAT, False, stride, ctypes.c_void_p(3 * 4))
gl.glEnableVertexAttribArray(1)
gl.glVertexAttribPointer(2, 3, GL_FLOAT, False, stride, ctypes.c_void_p(6 * 4))
gl.glEnableVertexAttribArray(2)

# Secondary Sphere / Object geometry
def make_sphere_data(radius, rings, sectors, color):
    r, g, b = color
    verts = []
    for i in range(rings):
        phi1 = math.pi * float(i) / rings
        phi2 = math.pi * float(i + 1) / rings
        for j in range(sectors):
            theta1 = 2.0 * math.pi * float(j) / sectors
            theta2 = 2.0 * math.pi * float(j + 1) / sectors
            
            p1 = [radius * math.sin(phi1) * math.cos(theta1), radius * math.cos(phi1), radius * math.sin(phi1) * math.sin(theta1)]
            p2 = [radius * math.sin(phi2) * math.cos(theta1), radius * math.cos(phi2), radius * math.sin(phi2) * math.sin(theta1)]
            p3 = [radius * math.sin(phi2) * math.cos(theta2), radius * math.cos(phi2), radius * math.sin(phi2) * math.sin(theta2)]
            p4 = [radius * math.sin(phi1) * math.cos(theta2), radius * math.cos(phi1), radius * math.sin(phi1) * math.sin(theta2)]

            def add_vert(p):
                l = math.sqrt(p[0]*p[0] + p[1]*p[1] + p[2]*p[2]) or 1.0
                verts.extend([p[0], p[1], p[2], p[0]/l, p[1]/l, p[2]/l, r, g, b])

            # Triangle 1: p1, p2, p3
            add_vert(p1); add_vert(p2); add_vert(p3)
            # Triangle 2: p1, p3, p4
            add_vert(p1); add_vert(p3); add_vert(p4)
    return verts

sphere_data = make_sphere_data(1.1, 24, 32, [0.22, 0.55, 0.90]) # Cyan-blue
sphere_vao = ctypes.c_uint()
gl.glGenVertexArrays(1, ctypes.byref(sphere_vao))
gl.glBindVertexArray(sphere_vao)
sphere_vbo = ctypes.c_uint()
gl.glGenBuffers(1, ctypes.byref(sphere_vbo))
gl.glBindBuffer(GL_ARRAY_BUFFER, sphere_vbo)
c_sphere = (ctypes.c_float * len(sphere_data))(*sphere_data)
gl.glBufferData(GL_ARRAY_BUFFER, ctypes.sizeof(c_sphere), c_sphere, GL_STATIC_DRAW)
gl.glVertexAttribPointer(0, 3, GL_FLOAT, False, stride, None)
gl.glEnableVertexAttribArray(0)
gl.glVertexAttribPointer(1, 3, GL_FLOAT, False, stride, ctypes.c_void_p(3 * 4))
gl.glEnableVertexAttribArray(1)
gl.glVertexAttribPointer(2, 3, GL_FLOAT, False, stride, ctypes.c_void_p(6 * 4))
gl.glEnableVertexAttribArray(2)

# Build 3D Transform Gizmo (Red X, Green Y, Blue Z arrows + cones)
# Pos 3, Color 4 = 7 floats
gizmo_lines = [
    # X Axis (Red)
    0.0, 0.0, 0.0, 0.95, 0.15, 0.15, 1.0,
    1.6, 0.0, 0.0, 0.95, 0.15, 0.15, 1.0,
    # Y Axis (Green)
    0.0, 0.0, 0.0, 0.15, 0.90, 0.20, 1.0,
    0.0, 1.6, 0.0, 0.15, 0.90, 0.20, 1.0,
    # Z Axis (Blue)
    0.0, 0.0, 0.0, 0.15, 0.35, 0.95, 1.0,
    0.0, 0.0, 1.6, 0.15, 0.35, 0.95, 1.0,
]

gizmo_vao = ctypes.c_uint()
gl.glGenVertexArrays(1, ctypes.byref(gizmo_vao))
gl.glBindVertexArray(gizmo_vao)
gizmo_vbo = ctypes.c_uint()
gl.glGenBuffers(1, ctypes.byref(gizmo_vbo))
gl.glBindBuffer(GL_ARRAY_BUFFER, gizmo_vbo)
c_gizmo = (ctypes.c_float * len(gizmo_lines))(*gizmo_lines)
gl.glBufferData(GL_ARRAY_BUFFER, ctypes.sizeof(c_gizmo), c_gizmo, GL_STATIC_DRAW)
gl.glVertexAttribPointer(0, 3, GL_FLOAT, False, 7 * 4, None)
gl.glEnableVertexAttribArray(0)
gl.glVertexAttribPointer(1, 4, GL_FLOAT, False, 7 * 4, ctypes.c_void_p(3 * 4))
gl.glEnableVertexAttribArray(1)

# Render Scene
gl.glClearColor(ctypes.c_float(0.12), ctypes.c_float(0.13), ctypes.c_float(0.16), ctypes.c_float(1.0)) # Engine dark slate background
gl.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT)

# 1. Render Ground Grid
gl.glUseProgram(grid_prog)
set_uniform_mat4(grid_prog, "uView", view_mat)
set_uniform_mat4(grid_prog, "uProj", proj_mat)
gl.glBindVertexArray(grid_vao)
gl.glDrawArrays(GL_TRIANGLES, 0, 6)

# 2. Render 3D Objects
gl.glUseProgram(mesh_prog)
set_uniform_mat4(mesh_prog, "uView", view_mat)
set_uniform_mat4(mesh_prog, "uProj", proj_mat)
set_uniform_vec3(mesh_prog, "uLightDir", [0.55, 0.85, 0.45])
set_uniform_vec3(mesh_prog, "uViewPos", eye)

# Draw Cube at [0, 1.0, 0]
cube_model = mat4_mult(mat4_translate(0.0, 1.0, 0.0), mat4_scale(1.0, 1.0, 1.0))
set_uniform_mat4(mesh_prog, "uModel", cube_model)
gl.glBindVertexArray(cube_vao)
gl.glDrawArrays(GL_TRIANGLES, 0, 36)

# Draw Sphere at [-2.8, 1.1, -1.2]
sphere_model = mat4_translate(-2.8, 1.1, -1.2)
set_uniform_mat4(mesh_prog, "uModel", sphere_model)
gl.glBindVertexArray(sphere_vao)
gl.glDrawArrays(GL_TRIANGLES, 0, len(sphere_data) // 9)

# Draw Plane platform at [0, 0.01, 0]
platform_data = make_cube_data([0.28, 0.30, 0.35])
platform_model = mat4_mult(mat4_translate(0.0, 0.05, 0.0), mat4_scale(4.5, 0.05, 4.5))
set_uniform_mat4(mesh_prog, "uModel", platform_model)
gl.glBindVertexArray(cube_vao)
gl.glDrawArrays(GL_TRIANGLES, 0, 36)

# 3. Render 3D Transform Gizmo centered on selected Cube [0, 1.0, 0]
# Disable depth test for gizmo so it renders cleanly over the selected object
gl.glDisable(GL_DEPTH_TEST)
gl.glLineWidth(ctypes.c_float(3.5))
gl.glUseProgram(gizmo_prog)
set_uniform_mat4(gizmo_prog, "uView", view_mat)
set_uniform_mat4(gizmo_prog, "uProj", proj_mat)
gizmo_model = mat4_translate(0.0, 1.0, 0.0)
set_uniform_mat4(gizmo_prog, "uModel", gizmo_model)
gl.glBindVertexArray(gizmo_vao)
gl.glDrawArrays(GL_LINES, 0, 6)

gl.glFinish()

# Read back pixels
pixel_buffer = (ctypes.c_ubyte * (WIDTH * HEIGHT * 4))()
gl.glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixel_buffer)

# Export image
output_dir = Path("/home/antigravity/UniVexStudio/UniVex_Engine")
output_path = output_dir / "univex_actual_render.png"

raw_bytes = bytes(pixel_buffer)
img = Image.frombytes("RGBA", (WIDTH, HEIGHT), raw_bytes)
img = img.transpose(Image.FLIP_TOP_BOTTOM)
img.save(str(output_path), "PNG")

print(f"[UniVex Render] SUCCESS! Actual frame rendered to: {output_path}")
print(f"[UniVex Render] Resolution: {WIDTH}x{HEIGHT}, Mode: RGBA8")
