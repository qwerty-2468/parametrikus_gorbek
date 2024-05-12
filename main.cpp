//=============================================================================================
// Mintaprogram: Zöld háromszög. Ervenyes 2019. osztol.
//
// A beadott program csak ebben a fajlban lehet, a fajl 1 byte-os ASCII karaktereket tartalmazhat, BOM kihuzando.
// Tilos:
// - mast "beincludolni", illetve mas konyvtarat hasznalni
// - faljmuveleteket vegezni a printf-et kiveve
// - Mashonnan atvett programresszleteket forrasmegjeloles nelkul felhasznalni es
// - felesleges programsorokat a beadott programban hagyni!!!!!!! 
// - felesleges kommenteket a beadott programba irni a forrasmegjelolest kommentjeit kiveve
// ---------------------------------------------------------------------------------------------
// A feladatot ANSI C++ nyelvu forditoprogrammal ellenorizzuk, a Visual Studio-hoz kepesti elteresekrol
// es a leggyakoribb hibakrol (pl. ideiglenes objektumot nem lehet referencia tipusnak ertekul adni)
// a hazibeado portal ad egy osszefoglalot.
// ---------------------------------------------------------------------------------------------
// A feladatmegoldasokban csak olyan OpenGL fuggvenyek hasznalhatok, amelyek az oran a feladatkiadasig elhangzottak 
// A keretben nem szereplo GLUT fuggvenyek tiltottak.
//
// NYILATKOZAT
// ---------------------------------------------------------------------------------------------
// Nev    : Vizhányó Miklós Ferenc
// Neptun : NVY1AG
// ---------------------------------------------------------------------------------------------
// ezennel kijelentem, hogy a feladatot magam keszitettem, es ha barmilyen segitseget igenybe vettem vagy
// mas szellemi termeket felhasznaltam, akkor a forrast es az atvett reszt kommentekben egyertelmuen jeloltem.
// A forrasmegjeloles kotelme vonatkozik az eloadas foliakat es a targy oktatoi, illetve a
// grafhazi doktor tanacsait kiveve barmilyen csatornan (szoban, irasban, Interneten, stb.) erkezo minden egyeb
// informaciora (keplet, program, algoritmus, stb.). Kijelentem, hogy a forrasmegjelolessel atvett reszeket is ertem,
// azok helyessegere matematikai bizonyitast tudok adni. Tisztaban vagyok azzal, hogy az atvett reszek nem szamitanak
// a sajat kontribucioba, igy a feladat elfogadasarol a tobbi resz mennyisege es minosege alapjan szuletik dontes.
// Tudomasul veszem, hogy a forrasmegjeloles kotelmenek megsertese eseten a hazifeladatra adhato pontokat
// negativ elojellel szamoljak el es ezzel parhuzamosan eljaras is indul velem szemben.
//=============================================================================================
#include "framework.h"

using namespace std;

const char * const vertexSource = R"(
    #version 330
    precision highp float;
    
    uniform mat4 MVP;
    layout(location = 0) in vec2 vertexPosition;
    void main() {
        gl_Position = vec4(vertexPosition.x, vertexPosition.y, 0, 1) * MVP;
    }
)";


const char * const fragmentSource = R"(
    #version 330
    precision highp float;
    
    uniform vec3 color;
    out vec4 outColor;

    void main() {
        outColor = vec4(color, 1);
    }
)";

GPUProgram gpuProgram;
unsigned int vao;

enum State {LAGRANGE, BEZIER, CATMULL_ROM};
State s = LAGRANGE;
vec2 * pickedPoint1 = nullptr;

class Camera2D {
    vec2 wCenter;
    vec2 wSize;
public:
    Camera2D(){
        wCenter = vec2(0,0);
        wSize = vec2(30,30);
    }
    mat4 V() { return TranslateMatrix(-wCenter); }
    mat4 P() {
        return ScaleMatrix(vec2(2/wSize.x, 2/wSize.y));
    }
    mat4 Vinv() {
        return TranslateMatrix(wCenter);
    }
    mat4 Pinv() {
        return ScaleMatrix(vec2(wSize.x/2, wSize.y/2));
    }
    void Zoom(float s) { wSize = wSize * s; }
    void Pan(vec2 t) { wCenter = wCenter + t; }
    vec2 PixelToNDC(int pX, int pY) {
        vec4 noCam = vec4(2.0f * pX / windowWidth - 1, 1 - 2.0f * pY / windowHeight, 0, 1);
        vec4 withCam = noCam*Pinv()*Vinv();
        return vec2(withCam.x, withCam.y);
    }
};

Camera2D camera;

class Object {
    unsigned int vao = 0, vbo = 0, nVtx = 0;
protected:
    vec2 scale = vec2(1, 1), pos = vec2(0, 0);
    float phi = 0;
public:
    Object() {
        glGenVertexArrays(1, &vao); glBindVertexArray(vao);
        glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0,NULL);
    }
    virtual vector<vec2> GenVertexData() = 0;
    void UpdateGPU() {
        glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
        vector<vec2> vtx = GenVertexData(); nVtx = vtx.size();
        glBufferData(GL_ARRAY_BUFFER, nVtx * sizeof(vec2), &vtx[0], GL_DYNAMIC_DRAW);
    }
    void Draw(int type, int index, int size, vec3 color) {
        if (nVtx == 0) return;
        mat4 MVP = camera.V() * camera.P();
        gpuProgram.setUniform(MVP, "MVP");
        gpuProgram.setUniform(color, "color");
        glBindVertexArray(vao); glDrawArrays(type, index, size);
    }
};

float tension = 0;

class Curve : public Object{
protected:
    vector<float> ts;
    vector<vec2> cps;
    float sumLength = 0;
public:
    const int nTessVertices = 100;
    void AddControlPoint(vec2 cp) {
        cps.push_back(cp);
        if(cps.size() > 1)
            sumLength += length(cps[cps.size()-1] - cps[cps.size()-2]);
        update();
    }
    virtual vec2 r(float t)=0;
    vector<vec2> GenVertexData() {
        vector<vec2> vertices;
        for (vec2 v : cps) {
            vertices.push_back(v);
        }
        if(cps.size()>1){
            for(int i = 0; i <= nTessVertices; ++i) {
                float t = (float)i / nTessVertices;
                vertices.push_back(r(t));
            }
        }
        return vertices;
    }
    void update() {
        ts.clear();
        if(cps.size()==1){
            UpdateGPU();
            Draw(GL_POINTS, 0, cps.size(), vec3(1, 0, 0));
            ts.push_back(0.0);
        }
        if(cps.size()>1){
            ts.push_back(0.0);
            float sumI = 0;
            sumLength = 0;
            for(int i=1; i<cps.size(); i++){
                sumLength += length(cps[i] - cps[i-1]);
            }
            for(int i=1; i<cps.size(); i++){
                sumI += length(cps[i] - cps[i-1]);
                ts.push_back(sumI/sumLength);
            }
            UpdateGPU();
            Draw(GL_LINE_STRIP, cps.size(), nTessVertices+1, vec3(1, 1, 0));
            Draw(GL_POINTS, 0, cps.size(), vec3(1, 0, 0));
        }
    }
    vec2 *pickPoint(vec2 pp) {
        for (auto& p : cps) if (length(pp - p) < 0.1) return &p;
        return nullptr;
    }
};

class Lagrange : public Curve {
    float L(int i, float t) {
        float Li = 1.0f;
        for(int j = 0; j < cps.size(); j++)
            if (j != i) Li *= (t - ts[j])/(ts[i] - ts[j]);
        return Li;
    }
public:
    vec2 r(float t) {
        vec2 rt(0, 0);
        for(int i = 0; i < cps.size(); i++){
            rt.x += cps[i].x * L(i,t);
            rt.y += cps[i].y * L(i,t);
        }
        return rt;
    }
};

class Bezier : public Curve {
    float B(int i, float t) {
        int n = cps.size()-1;
        float choose = 1;
        for(int j = 1; j <= i; j++) choose *= (float)(n-j+1)/j;
        return choose * pow(t, i) * pow(1-t, n-i);
    }
public:
    vec2 r(float t) {
        vec2 rt(0, 0);
        for(int i=0; i < cps.size(); i++) rt = rt + cps[i] * B(i,t);
        return rt;
    }
};

class CatmullRom : public Curve{
    vec2 Hermite(vec2 p0, vec2 v0, float t0, vec2 p1, vec2 v1, float t1, float t) {
        vec2 a0 = p0;
        vec2 a1 = v0;
        vec2 a2 = (3 * (p1 - p0) / pow(t1 - t0, 2)) - ((v1 + 2 * v0) / (t1 - t0));
        vec2 a3 = (2 * (p0 - p1) / pow(t1 - t0,3)) + ((v1 + v0) / (pow(t1 - t0,2)));
        return a3 * pow(t-t0, 3) + a2 * pow(t-t0, 2) + a1 * (t-t0) + a0;
        
    }
public:
    vec2 r(float t) {
        for(int i = 0; i < cps.size() - 1; i++)
            if (ts[i] <= t && t <= ts[i+1]) {
                vec2 v0, v1;
                if (i == 0){
                    v0 = ((1.0f - tension) / 2.0f) * ((cps[i+1] - cps[i]) / ((ts[i+1] - ts[i])));
                    v1 = ((1.0f - tension) / 2.0f) * ((cps[i+1+1] - cps[i+1]) / ((ts[i+1+1] - ts[i+1])) + (cps[i+1] - cps[i-1+1]) / (ts[i+1] - ts[i-1+1]));
                }
                else if (i == cps.size() - 2){
                    v0 = ((1.0f - tension) / 2.0f) * ((cps[i+1] - cps[i]) / ((ts[i+1] - ts[i])) + (cps[i] - cps[i - 1]) / (ts[i] - ts[i-1]));
                    v1 = ((1.0f - tension) / 2.0f) * ((cps[i+1] - cps[i-1+1]) / (ts[i+1] - ts[i-1+1]));
                }
                else{
                    v0 = ((1.0f - tension) / 2.0f) * ((cps[i+1] - cps[i]) / ((ts[i+1] - ts[i])) + (cps[i] - cps[i - 1]) / (ts[i] - ts[i-1]));
                    v1 = ((1.0f - tension) / 2.0f) * ((cps[i+1+1] - cps[i+1]) / ((ts[i+1+1] - ts[i+1])) + (cps[i+1] - cps[i-1+1]) / (ts[i+1] - ts[i-1+1]));
                }
                return Hermite(cps[i], v0, ts[i], cps[i+1], v1, ts[i+1], t);
            }
    return vec2(0,0);
    }
};

Curve * c;

void onKeyboard(unsigned char key, int pX, int pY) {
    switch(key) {
        case 'l':
            s = LAGRANGE;
            c = new Lagrange;
            pickedPoint1 = nullptr;
            printf("LAGRANGE\n");
            break;
            
        case 'b':
            s = BEZIER;
            c = new Bezier;
            pickedPoint1 = nullptr;
            printf("BEZIER\n");
            break;
            
        case 'c':
            s = CATMULL_ROM;
            c = new CatmullRom;
            pickedPoint1 = nullptr;
            printf("CATMULL-ROM\n");
            break;
            
        case 'p':
            camera.Pan(-1);
            break;
            
        case 'P':
            camera.Pan(1);
            break;
            
        case 'z':
            camera.Zoom(1/1.1);
            break;
            
        case 'Z':
            camera.Zoom(1.1);
            break;
            
        case 't':
            tension-=0.1;
            c->update();
            break;
            
        case 'T':
            tension+=0.1;
            c->update();
            break;
    }
    glutPostRedisplay();
}

void onKeyboardUp(unsigned char key, int pX, int pY) {
}

void onIdle() {
    long time = glutGet(GLUT_ELAPSED_TIME);
}

void onMouse(int button, int state, int pX, int pY) {
    if (button == GLUT_LEFT_BUTTON && state == GLUT_DOWN) {
        c->AddControlPoint(camera.PixelToNDC(pX, pY));
        glutPostRedisplay();
    }
    if (button == GLUT_RIGHT_BUTTON && state == GLUT_DOWN)
        pickedPoint1 = c->pickPoint(camera.PixelToNDC(pX, pY));
    if (button == GLUT_RIGHT_BUTTON && state == GLUT_UP)
        pickedPoint1 = nullptr;
}
void onMouseMotion(int pX, int pY) {
    if (pickedPoint1) {
        *pickedPoint1 = vec2(camera.PixelToNDC(pX, pY));
        c->update(); glutPostRedisplay();
    }
}

void onInitialization() {
    glViewport(0, 0, windowWidth, windowHeight);
    glLineWidth(2);
    glPointSize(10);
    c = new Lagrange();
    gpuProgram.create(vertexSource, fragmentSource, "fragmentColor");
}

void onDisplay() {
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    c->update();
    glutSwapBuffers();
}
