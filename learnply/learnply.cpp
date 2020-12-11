#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <fstream>
#include <vector>
#include <algorithm>
#include <iostream>

#include "glError.h"
#include "gl/glew.h"
#include "gl/freeglut.h"
#include "ply.h"
#include "icVector.H"
#include "icMatrix.H"
#include "polyhedron.h"
#include "polyline.h"
#include "trackball.h"
#include "tmatrix.h"

Polyhedron* poly;

/*scene related variables*/
const float zoomspeed = 0.9;
const int view_mode = 0;		// 0 = othogonal, 1=perspective
const double radius_factor = 1.0;
int win_width = 800;
int win_height = 800;
float aspectRatio = win_width / win_height;
/*
Use keys 1 to 0 to switch among different display modes.
Each display mode can be designed to show one type 
visualization result.

Predefined ones: 
display mode 1: solid rendering
display mode 2: show wireframes
display mode 3: render each quad with colors of vertices
*/
int display_mode = 1;

/*User Interaction related variabes*/
float s_old, t_old;
float rotmat[4][4];
double zoom = 1.0;
double translation[2] = { 0, 0 };
int mouse_mode = -2;	// -1 = no action, 1 = tranlate y, 2 = rotate

/*IBFV related variables*/
//https://www.win.tue.nl/~vanwijk/ibfv/
#define	NPN 64
#define SCALE 4.0
int    Npat = 32;
int    iframe = 0;
float  tmax = win_width / (SCALE*NPN);
float  dmax = SCALE / win_width;
unsigned char *pixels;
bool showPickedPoint;
icVector3 pickedPoint;
PolyLine streamline_picked;
FILE* this_file;

#define DM  ((float) (1.0/(100-1.0)))

/******************************************************************************
Forward declaration of functions
******************************************************************************/

void init(void);
void makePatterns(void);

/*glut attaching functions*/
void keyboard(unsigned char key, int x, int y);
void motion(int x, int y);
void display(void);
void mouse(int button, int state, int x, int y);
void mousewheel(int wheel, int direction, int x, int y);
void reshape(int width, int height);

/*functions for element picking*/
void display_vertices(GLenum mode, Polyhedron* poly);
void display_quads(GLenum mode, Polyhedron* poly);
void display_selected_vertex(Polyhedron* poly);
void display_selected_quad(Polyhedron* poly);

/*display vis results*/
void display_polyhedron(Polyhedron* poly);

/*display utilities*/

/*
draw a sphere
x, y, z are the coordiate of the dot
radius of the sphere 
R: the red channel of the color, ranges [0, 1]
G: the green channel of the color, ranges [0, 1]
B: the blue channel of the color, ranges [0, 1]
*/
void drawDot(double x, double y, double z, double radius = 0.1, double R = 1.0, double G = 0.0, double B = 0.0) {

	glEnable(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(1., 1.);
	glEnable(GL_DEPTH_TEST);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glShadeModel(GL_SMOOTH);
	glEnable(GL_LIGHTING);
	glEnable(GL_LIGHT0);
	glEnable(GL_LIGHT1);

	GLfloat mat_diffuse[4];

	{
		mat_diffuse[0] = R;
		mat_diffuse[1] = G;
		mat_diffuse[2] = B;
		mat_diffuse[3] = 1.0;
	}

	glMaterialfv(GL_FRONT, GL_DIFFUSE, mat_diffuse);

	GLUquadric* quad = gluNewQuadric();

	glPushMatrix();
	glTranslatef(x, y, z);
	gluSphere(quad, radius, 50, 50);
	glPopMatrix();

	gluDeleteQuadric(quad);
}

/*
draw a line segment
width: the width of the line, should bigger than 0
R: the red channel of the color, ranges [0, 1]
G: the green channel of the color, ranges [0, 1]
B: the blue channel of the color, ranges [0, 1]
*/
void drawLineSegment(LineSegment ls, double width = 1.0, double R = 1.0, double G = 0.0, double B = 0.0) {

	glDisable(GL_LIGHTING);
	glEnable(GL_LINE_SMOOTH);
	glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glLineWidth(width);

	glBegin(GL_LINES);
	glColor3f(R, G, B);
	glVertex3f(ls.start.x, ls.start.y, ls.start.z);
	glVertex3f(ls.end.x, ls.end.y, ls.end.z);
	glEnd();

	glDisable(GL_BLEND);
}

/*
draw a polyline
width: the width of the line, should bigger than 0
R: the red channel of the color, ranges [0, 1]
G: the green channel of the color, ranges [0, 1]
B: the blue channel of the color, ranges [0, 1]
*/
void drawPolyline(PolyLine pl, double width = 1.0, double R = 1.0, double G = 0.0, double B = 0.0) {
	
	glDisable(GL_LIGHTING);
	glEnable(GL_LINE_SMOOTH);
	glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glLineWidth(width);

	glBegin(GL_LINES);
	glColor3f(R, G, B);

	for (int i = 0; i < pl.size(); i++) {
		glVertex3f(pl[i].start.x, pl[i].start.y, pl[i].start.z);
		glVertex3f(pl[i].end.x, pl[i].end.y, pl[i].end.z);
	}

	glEnd();

	glDisable(GL_BLEND);
}

/******************************************************************************
Main program.
******************************************************************************/
int main(int argc, char* argv[])
{
	/*load mesh from ply file*/
	char filename[255];

	printf("Enter filename:\n");
	fgets(filename, sizeof(filename), stdin);
	filename[strlen(filename)-1] = '\0';

	char path[255];
	path[0] = '\0';
	strcat(path, "../quadmesh_2D/vector_data/");
	strcat(path, filename);
	if ((this_file = fopen(path, "r")) == NULL) {
		perror("Failed to open the file");
		exit(1);
	}

	//FILE* this_file = fopen("../quadmesh_2D/vector_data/v1.ply", "r");

	poly = new Polyhedron(this_file);
	fclose(this_file);
	
	/*initialize the mesh*/
	poly->initialize(); // initialize the mesh
	poly->write_info();


	/*init glut and create window*/
	glutInit(&argc, argv);
	glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB);
	glutInitWindowPosition(20, 20);
	glutInitWindowSize(win_width, win_height);
	glutCreateWindow("Scientific Visualization");


	/*initialize openGL*/
	init();

	/*prepare the noise texture for IBFV*/
	makePatterns();
	
	/*the render function and callback registration*/
	glutKeyboardFunc(keyboard);
	glutReshapeFunc(reshape);
	glutDisplayFunc(display);
	glutIdleFunc(display);
	glutMotionFunc(motion);
	glutMouseFunc(mouse);
	glutMouseWheelFunc(mousewheel);
	
	/*event processing loop*/
	glutMainLoop();
	
	/*clear memory before exit*/
	poly->finalize();	// finalize everything
	free(pixels);
	return 0;
}


/******************************************************************************
Set projection mode
******************************************************************************/

void set_view(GLenum mode)
{
	GLfloat light_ambient0[] = { 0.3, 0.3, 0.3, 1.0 };
	GLfloat light_diffuse0[] = { 0.7, 0.7, 0.7, 1.0 };
	GLfloat light_specular0[] = { 0.0, 0.0, 0.0, 1.0 };

	GLfloat light_ambient1[] = { 0.0, 0.0, 0.0, 1.0 };
	GLfloat light_diffuse1[] = { 0.5, 0.5, 0.5, 1.0 };
	GLfloat light_specular1[] = { 0.0, 0.0, 0.0, 1.0 };

	glLightfv(GL_LIGHT0, GL_AMBIENT, light_ambient0);
	glLightfv(GL_LIGHT0, GL_DIFFUSE, light_diffuse0);
	glLightfv(GL_LIGHT0, GL_SPECULAR, light_specular0);

	glLightfv(GL_LIGHT1, GL_AMBIENT, light_ambient1);
	glLightfv(GL_LIGHT1, GL_DIFFUSE, light_diffuse1);
	glLightfv(GL_LIGHT1, GL_SPECULAR, light_specular1);


	glMatrixMode(GL_PROJECTION);
	if (mode == GL_RENDER)
		glLoadIdentity();

	if (aspectRatio >= 1.0) {
		if (view_mode == 0)
			glOrtho(-radius_factor * zoom * aspectRatio, radius_factor * zoom * aspectRatio, -radius_factor * zoom, radius_factor * zoom, -1000, 1000);
		else
			glFrustum(-radius_factor * zoom * aspectRatio, radius_factor * zoom * aspectRatio, -radius_factor* zoom, radius_factor* zoom, 0.1, 1000);
	}
	else {
		if (view_mode == 0)
			glOrtho(-radius_factor * zoom, radius_factor * zoom, -radius_factor * zoom / aspectRatio, radius_factor * zoom / aspectRatio, -1000, 1000);
		else
			glFrustum(-radius_factor * zoom, radius_factor * zoom, -radius_factor* zoom / aspectRatio, radius_factor* zoom / aspectRatio, 0.1, 1000);
	}


	GLfloat light_position[3];
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	light_position[0] = 5.5;
	light_position[1] = 0.0;
	light_position[2] = 0.0;
	glLightfv(GL_LIGHT0, GL_POSITION, light_position);
	light_position[0] = -0.1;
	light_position[1] = 0.0;
	light_position[2] = 0.0;
	glLightfv(GL_LIGHT2, GL_POSITION, light_position);
}

/******************************************************************************
Update the scene
******************************************************************************/

void set_scene(GLenum mode, Polyhedron* poly)
{
	glTranslatef(translation[0], translation[1], -3.0);

	/*multiply rotmat to current mat*/
	{
		int i, j, index = 0;

		GLfloat mat[16];

		for (i = 0; i < 4; i++)
			for (j = 0; j < 4; j++)
				mat[index++] = rotmat[i][j];

		glMultMatrixf(mat);
	}

	glScalef(0.9 / poly->radius, 0.9 / poly->radius, 0.9 / poly->radius);
	glTranslatef(-poly->center.entry[0], -poly->center.entry[1], -poly->center.entry[2]);
}


/******************************************************************************
Init scene
******************************************************************************/

void init(void) {

	mat_ident(rotmat);

	/* select clearing color */
	glClearColor(0.0, 0.0, 0.0, 0.0);  // background
	glShadeModel(GL_FLAT);
	glPolygonMode(GL_FRONT, GL_FILL);

	glDisable(GL_DITHER);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	
	//set pixel storage modes
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	
	glEnable(GL_NORMALIZE);
	if (poly->orientation == 0)
		glFrontFace(GL_CW);
	else
		glFrontFace(GL_CCW);
}


/******************************************************************************
Pick objects from the scene
******************************************************************************/

int processHits(GLint hits, GLuint buffer[])
{
	unsigned int i, j;
	GLuint names, * ptr;
	double smallest_depth = 1.0e+20, current_depth;
	int seed_id = -1;
	unsigned char need_to_update;

	ptr = (GLuint*)buffer;
	for (i = 0; i < hits; i++) {  /* for each hit  */
		need_to_update = 0;
		names = *ptr;
		ptr++;

		current_depth = (double)*ptr / 0x7fffffff;
		if (current_depth < smallest_depth) {
			smallest_depth = current_depth;
			need_to_update = 1;
		}
		ptr++;
		current_depth = (double)*ptr / 0x7fffffff;
		if (current_depth < smallest_depth) {
			smallest_depth = current_depth;
			need_to_update = 1;
		}
		ptr++;
		for (j = 0; j < names; j++) {  /* for each name */
			if (need_to_update == 1)
				seed_id = *ptr - 1;
			ptr++;
		}
	}
	return seed_id;
}

/******************************************************************************
Diaplay all quads for selection
******************************************************************************/

void display_quads(GLenum mode, Polyhedron* this_poly)
{
	unsigned int i, j;
	GLfloat mat_diffuse[4];

	glEnable(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(1., 1.);
	glEnable(GL_DEPTH_TEST);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glShadeModel(GL_SMOOTH);
	//glDisable(GL_LIGHTING);

	glEnable(GL_LIGHTING);
	glEnable(GL_LIGHT0);
	glEnable(GL_LIGHT1);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

	for (i = 0; i < this_poly->nquads; i++) {
		if (mode == GL_SELECT)
			glLoadName(i + 1);

		Quad* temp_q = this_poly->qlist[i];
		{
			mat_diffuse[0] = 1.0;
			mat_diffuse[1] = 1.0;
			mat_diffuse[2] = 0.0;
			mat_diffuse[3] = 1.0;
		}
		glMaterialfv(GL_FRONT, GL_DIFFUSE, mat_diffuse);
		
		glBegin(GL_POLYGON);
		for (j = 0; j < 4; j++) {
			Vertex* temp_v = temp_q->verts[j];
			//glColor3f(0, 0, 0);
			glVertex3d(temp_v->x, temp_v->y, temp_v->z);
		}
		glEnd();
	}
}

/******************************************************************************
Diaplay all vertices for selection
******************************************************************************/

void display_vertices(GLenum mode, Polyhedron* this_poly)
{
	glEnable(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(1., 1.);
	glEnable(GL_DEPTH_TEST);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glShadeModel(GL_SMOOTH);
	glDisable(GL_LIGHTING);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

	for (int i = 0; i < this_poly->nverts; i++) {
		if (mode == GL_SELECT)
			glLoadName(i + 1);

		Vertex* temp_v = this_poly->vlist[i];

		{
			GLUquadric* quad = gluNewQuadric();

			glPushMatrix();
			glTranslatef(temp_v->x, temp_v->y, temp_v->z);
			glColor4f(0, 0, 1, 1.0);
			gluSphere(quad, this_poly->radius * 0.01, 50, 50);
			glPopMatrix();

			gluDeleteQuadric(quad);
		}
	}
}

/******************************************************************************
Diaplay selected quad
******************************************************************************/

void display_selected_quad(Polyhedron* this_poly)
{
	if (this_poly->selected_quad == -1)
	{
		return;
	}

	unsigned int i, j;
	GLfloat mat_diffuse[4];

	glEnable(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(1., 1.);
	glDisable(GL_DEPTH_TEST);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glShadeModel(GL_SMOOTH);
	glDisable(GL_LIGHTING);

	Quad* temp_q = this_poly->qlist[this_poly->selected_quad];

	glBegin(GL_POLYGON);
	for (j = 0; j < 4; j++) {
		Vertex* temp_v = temp_q->verts[j];
		glColor3f(1.0, 0.0, 1.0);
		glVertex3d(temp_v->x, temp_v->y, 0.0);
	}
	glEnd();
}

/******************************************************************************
Diaplay selected vertex
******************************************************************************/

void display_selected_vertex(Polyhedron* this_poly)
{
	if (this_poly->selected_vertex == -1)
	{
		return;
	}

	Vertex* temp_v = this_poly->vlist[this_poly->selected_vertex];

	drawDot(temp_v->x, temp_v->y, temp_v->z, this_poly->radius * 0.01, 1.0, 0.0, 0.0);

}


/******************************************************************************
Callback function for glut window reshaped
******************************************************************************/

void reshape(int width, int height) {

	win_width = width;
	win_height = height;

	aspectRatio = (float)width / (float)height;

	glViewport(0, 0, width, height);

	set_view(GL_RENDER);

	/*Update pixels buffer*/
	free(pixels);
	pixels = (unsigned char *)malloc(sizeof(unsigned char)*win_width*win_height * 3);
	memset(pixels, 255, sizeof(unsigned char)*win_width*win_height * 3);
}


/******************************************************************************
Callback function for dragging mouse
******************************************************************************/

void motion(int x, int y) {
	float r[4];
	float s, t;

	s = (2.0 * x - win_width) / win_width;
	t = (2.0 * (win_height - y) - win_height) / win_height;

	if ((s == s_old) && (t == t_old))
		return;

	switch (mouse_mode) {
	case 2:

		Quaternion rvec;

		mat_to_quat(rotmat, rvec);
		trackball(r, s_old, t_old, s, t);
		add_quats(r, rvec, rvec);
		quat_to_mat(rvec, rotmat);

		s_old = s;
		t_old = t;

		display();
		break;

	case 1:

		translation[0] += (s - s_old);
		translation[1] += (t - t_old);

		s_old = s;
		t_old = t;

		display();
		break;
	}
}

/******************************************************************************
Callback function for mouse clicks
******************************************************************************/

void mouse(int button, int state, int x, int y) {

	int key = glutGetModifiers();

	if (button == GLUT_LEFT_BUTTON || button == GLUT_RIGHT_BUTTON) {
		
		if (state == GLUT_DOWN) {
			float xsize = (float)win_width;
			float ysize = (float)win_height;

			float s = (2.0 * x - win_width) / win_width;
			float t = (2.0 * (win_height - y) - win_height) / win_height;

			s_old = s;
			t_old = t;

			/*translate*/
			if (button == GLUT_LEFT_BUTTON)
			{
				mouse_mode = 1;
			}

			/*rotate*/
			if (button == GLUT_RIGHT_BUTTON)
			{
				mouse_mode = 2;
			}
		}
		else if (state == GLUT_UP) {

			if (button == GLUT_LEFT_BUTTON && key == GLUT_ACTIVE_SHIFT) {  // build up the selection feedback mode

				/*select face*/

				GLuint selectBuf[512];
				GLint hits;
				GLint viewport[4];

				glGetIntegerv(GL_VIEWPORT, viewport);

				glSelectBuffer(win_width, selectBuf);
				(void)glRenderMode(GL_SELECT);

				glInitNames();
				glPushName(0);

				glMatrixMode(GL_PROJECTION);
				glPushMatrix();
				glLoadIdentity();

				/*create 5x5 pixel picking region near cursor location */
				gluPickMatrix((GLdouble)x, (GLdouble)(viewport[3] - y), 1.0, 1.0, viewport);

				set_view(GL_SELECT);
				set_scene(GL_SELECT, poly);
				display_quads(GL_SELECT, poly);

				glMatrixMode(GL_PROJECTION);
				glPopMatrix();
				glFlush();

				glMatrixMode(GL_MODELVIEW);

				hits = glRenderMode(GL_RENDER);
				poly->selected_quad = processHits(hits, selectBuf);
				printf("Selected quad id = %d\n", poly->selected_quad);
				glutPostRedisplay();

			}
			else if (button == GLUT_LEFT_BUTTON && key == GLUT_ACTIVE_CTRL)
			{
				/*select vertex*/

				GLuint selectBuf[512];
				GLint hits;
				GLint viewport[4];

				glGetIntegerv(GL_VIEWPORT, viewport);

				glSelectBuffer(win_width, selectBuf);
				(void)glRenderMode(GL_SELECT);

				glInitNames();
				glPushName(0);

				glMatrixMode(GL_PROJECTION);
				glPushMatrix();
				glLoadIdentity();

				/*  create 5x5 pixel picking region near cursor location */
				gluPickMatrix((GLdouble)x, (GLdouble)(viewport[3] - y), 1.0, 1.0, viewport);

				set_view(GL_SELECT);
				set_scene(GL_SELECT, poly);
				display_vertices(GL_SELECT, poly);

				glMatrixMode(GL_PROJECTION);
				glPopMatrix();
				glFlush();

				glMatrixMode(GL_MODELVIEW);

				hits = glRenderMode(GL_RENDER);
				poly->selected_vertex = processHits(hits, selectBuf);
				printf("Selected vert id = %d\n", poly->selected_vertex);
				glutPostRedisplay();

			}

			mouse_mode = -1;
		}
	}
}

/******************************************************************************
Callback function for mouse wheel scroll
******************************************************************************/

void mousewheel(int wheel, int direction, int x, int y) {
	if (direction == 1) {
		zoom *= zoomspeed;
		glutPostRedisplay();
	}
	else if (direction == -1) {
		zoom /= zoomspeed;
		glutPostRedisplay();
	}
}

/*Display IBFV*/
void makePatterns(void)
{
	pixels = (unsigned char *)malloc(sizeof(unsigned char)*win_width*win_height * 3);
	memset(pixels, 255, sizeof(unsigned char)*win_width*win_height * 3);

	int lut[256];
	int phase[NPN][NPN];
	GLubyte pat[NPN][NPN][4];
	int i, j, k, t;

	for (i = 0; i < 256; i++) lut[i] = i < 127 ? 0 : 255;
	for (i = 0; i < NPN; i++)
		for (j = 0; j < NPN; j++) phase[i][j] = rand() % 256;

	for (k = 0; k < Npat; k++) {
		t = k * 256 / Npat;
		for (i = 0; i < NPN; i++)
			for (j = 0; j < NPN; j++) {
				pat[i][j][0] =
					pat[i][j][1] =
					pat[i][j][2] = lut[(t + phase[i][j]) % 255];
				pat[i][j][3] = int(0.12 * 255);
			}
		glNewList(k + 1, GL_COMPILE);
		glTexImage2D(GL_TEXTURE_2D, 0, 4, NPN, NPN, 0, GL_RGBA, GL_UNSIGNED_BYTE, pat);
		glEndList();
	}

}

void displayIBFV(void)
{
	glDisable(GL_LIGHTING);
	glDisable(GL_LIGHT0);
	glDisable(GL_LIGHT1);
	glDisable(GL_POLYGON_OFFSET_FILL);
	glDisable(GL_DEPTH_TEST);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

	glEnable(GL_TEXTURE_2D);
	glShadeModel(GL_FLAT);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glClearColor(1.0, 1.0, 1.0, 1.0);  // background for rendering color coding and lighting
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	/*draw the model with using the pixels, using vector field to advert the texture coordinates*/
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, win_width, win_height, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels);

	double modelview_matrix1[16], projection_matrix1[16];
	int viewport1[4];
	glGetDoublev(GL_MODELVIEW_MATRIX, modelview_matrix1);
	glGetDoublev(GL_PROJECTION_MATRIX, projection_matrix1);
	glGetIntegerv(GL_VIEWPORT, viewport1);

	for (int i = 0; i < poly->nquads; i++) { //go through all the quads

		Quad *temp_q = poly->qlist[i];

		glBegin(GL_QUADS);

		for (int j = 0; j < 4; j++) {
			Vertex *temp_v = temp_q->verts[j];

			double x = temp_v->x;
			double y = temp_v->y;

			double tx, ty, dummy;

			gluProject((GLdouble)temp_v->x, (GLdouble)temp_v->y, (GLdouble)temp_v->z,
				modelview_matrix1, projection_matrix1, viewport1, &tx, &ty, &dummy);

			tx = tx / win_width;
			ty = ty / win_height;

			icVector2 dp = icVector2(temp_v->vx, temp_v->vy);
			normalize(dp);

			double dx = dp.x;
			double dy = dp.y;

			double r = dx * dx + dy * dy;
			if (r > dmax*dmax) {
				r = sqrt(r);
				dx *= dmax / r;
				dy *= dmax / r;
			}

			float px = tx + dx;
			float py = ty + dy;

			glTexCoord2f(px, py);
			glVertex3d(temp_v->x, temp_v->y, temp_v->z);
		}
		glEnd();
	}

	iframe = iframe + 1;

	glEnable(GL_BLEND);

	/*blend the drawing with another noise image*/
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();


	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();

	glTranslatef(-1.0, -1.0, 0.0);
	glScalef(2.0, 2.0, 1.0);

	glCallList(iframe % Npat + 1);

	glBegin(GL_QUAD_STRIP);

	glTexCoord2f(0.0, 0.0);  glVertex2f(0.0, 0.0);
	glTexCoord2f(0.0, tmax); glVertex2f(0.0, 1.0);
	glTexCoord2f(tmax, 0.0);  glVertex2f(1.0, 0.0);
	glTexCoord2f(tmax, tmax); glVertex2f(1.0, 1.0);
	glEnd();
	glDisable(GL_BLEND);

	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();

	glMatrixMode(GL_PROJECTION);
	glPopMatrix();

	glReadPixels(0, 0, win_width, win_height, GL_RGB, GL_UNSIGNED_BYTE, pixels);


	/*draw the model with using pixels, note the tx and ty do not take the vector on points*/
	glClearColor(1.0, 1.0, 1.0, 1.0);  // background for rendering color coding and lighting
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, win_width, win_height, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels);
	for (int i = 0; i < poly->nquads; i++) { //go through all the quads
		Quad *temp_q = poly->qlist[i];
		glBegin(GL_QUADS);
		for (int j = 0; j < 4; j++) {
			Vertex *temp_v = temp_q->verts[j];
			double x = temp_v->x;
			double y = temp_v->y;
			double tx, ty, dummy;
			gluProject((GLdouble)temp_v->x, (GLdouble)temp_v->y, (GLdouble)temp_v->z,
				modelview_matrix1, projection_matrix1, viewport1, &tx, &ty, &dummy);
			tx = tx / win_width;
			ty = ty / win_height;
			glTexCoord2f(tx, ty);
			glVertex3d(temp_v->x, temp_v->y, temp_v->z);
		}
		glEnd();
	}

	glDisable(GL_TEXTURE_2D);
	glShadeModel(GL_SMOOTH);
	glDisable(GL_BLEND);
}

/******************************************************************************
Callback function for scene display
******************************************************************************/

void display(void)
{
	glClearColor(1.0, 1.0, 1.0, 1.0);  // background for rendering color coding and lighting

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	set_view(GL_RENDER);
	CHECK_GL_ERROR();

	set_scene(GL_RENDER, poly);
	CHECK_GL_ERROR();

	/*display the mesh*/
	display_polyhedron(poly);
	CHECK_GL_ERROR();

	/*display selected elements*/
	display_selected_vertex(poly);
	CHECK_GL_ERROR();

	display_selected_quad(poly);
	CHECK_GL_ERROR();

	glFlush();
	glutSwapBuffers();
	glFinish();

	CHECK_GL_ERROR();
}


/******************************************************************************
Process a keyboard action.  In particular, exit the program when an
"escape" is pressed in the window.
******************************************************************************/

/*global variable to save polylines*/
PolyLine pentagon;
std::vector<std::pair<PolyLine, float>> contours;
PolyLine contour_s;
std::vector<Vertex*> crossings;
double min, max;
std::vector<float> scalars;
std::vector<PolyLine> streamlines;

Vertex **white_noise_vlist;
Quad **white_noise_qlist;
int white_noise_nverts;
int white_noise_nquads;

icVector3 getDir(double x, double y, double z) {

	//find the quad it is in
	Quad *q = NULL;
	double x1, x2, y1, y2;
	for (int i = 0; i < poly->nquads; i++) {
		q = poly->qlist[i];

		x1 = x2 = q->verts[0]->x;
		y1 = y2 = q->verts[0]->y;

		//similar to how did we find the x1,x2,y1,y2 in hw2
		for (int j = 0; j < 4; j++) {

			// x1 is the smallest x coordinate of the 4 verts
			if (q->verts[j]->x < x1) {
				x1 = q->verts[j]->x;
			}

			// x2 is the biggest x coordinate of the 4 verts
			if (q->verts[j]->x > x2) {
				x2 = q->verts[j]->x;
			}

			// y1 is the smallest y coordinate of the 4 verts
			if (q->verts[j]->y < y1) {
				y1 = q->verts[j]->y;
			}

			// y2 is the biggest y coordinate of the 4 verts
			if (q->verts[j]->y > y2) {
				y2 = q->verts[j]->y;
			}
		}

		if (x <= x2 && x >= x1 && y <= y2 && y >= y1) {
			break;
		}
	}

	//interpolate the vector field to get the direction
	double x0 = x;
	double y0 = y;

	//find the k, which corresponds to the vertex with x1 and y1,
	int k;
	for (int i = 0; i < 4; i++) {
		if (q->verts[i]->x == x1 && q->verts[i]->y == y1) {
			k = i;
		}
	}

	//first interpolate vx
	double fx1y1 = q->verts[k]->vx;
	double fx2y1 = q->verts[(k + 1) % 4]->vx;
	double fx2y2 = q->verts[(k + 2) % 4]->vx;
	double fx1y2 = q->verts[(k + 3) % 4]->vx;

	double dir_x = ((x2-x0)/(x2-x1))*((y2-y0)/(y2-y1))*fx1y1 +\
				((x0-x1)/(x2-x1))*((y2-y0)/(y2-y1))*fx2y1 +\
				((x2-x0)/(x2-x1))*((y0-y1)/(y2-y1))*fx1y2 +\
				((x0-x1)/(x2-x1))*((y0-y1)/(y2-y1))*fx2y2;

	//then interpolate vy
	fx1y1 = q->verts[k]->vy;
	fx2y1 = q->verts[(k + 1) % 4]->vy;
	fx2y2 = q->verts[(k + 2) % 4]->vy;
	fx1y2 = q->verts[(k + 3) % 4]->vy;

	double dir_y = ((x2-x0)/(x2-x1))*((y2-y0)/(y2-y1))*fx1y1 +\
				((x0-x1)/(x2-x1))*((y2-y0)/(y2-y1))*fx2y1 +\
				((x2-x0)/(x2-x1))*((y0-y1)/(y2-y1))*fx1y2 +\
				((x0-x1)/(x2-x1))*((y0-y1)/(y2-y1))*fx2y2;

	return icVector3(dir_x, dir_y, 0);
}

void compute_white_noise() {

	for (int i = 0; i < poly->nverts; i++) {
		if (i == 0) {
			//create minx, miny, maxx, maxy variables in Polyhedron to save the dimension
			poly->minx = poly->vlist[i]->x;
			poly->maxx = poly->vlist[i]->x;
			poly->miny = poly->vlist[i]->y;
			poly->maxy = poly->vlist[i]->y;
		} else {
			if (poly->vlist[i]->x < poly->minx)
				poly->minx = poly->vlist[i]->x;
			if (poly->vlist[i]->x > poly->maxx)
				poly->maxx = poly->vlist[i]->x;
			if (poly->vlist[i]->y < poly->miny)
				poly->miny = poly->vlist[i]->y;
			if (poly->vlist[i]->y > poly->maxy)
				poly->maxy = poly->vlist[i]->y;
		}
	}

	int n = 50;
	int nverts = n * n;
	int nquads = (n-1) * (n-1);
	white_noise_nverts = nverts;
	white_noise_nquads = nquads;

	white_noise_vlist = new Vertex *[nverts];
	white_noise_qlist = new Quad *[nquads];

	double x = poly->minx;
	double y = poly->miny;
	double xstep = (poly->maxx - poly->minx) / (n-1);
	double ystep = (poly->maxy - poly->miny) / (n-1);

	for (int i = 0; i < n; i++) {
		x = poly->minx;
		for (int j = 0; j < n; j++) {
			white_noise_vlist[n*i + j] = new Vertex(x, y, 0);
			white_noise_vlist[n*i + j]->scalar = rand() % 256;
			x += xstep;
		}
		y += ystep;
	}

	for (int i = 0; i < n-1; i++) {
		for (int j = 0; j < n-1; j++) {
			int index = (n-1) * i + j;
			white_noise_qlist[index] = new Quad;
			white_noise_qlist[index]->verts[0] = white_noise_vlist[i * n + j];
			white_noise_qlist[index]->verts[1] = white_noise_vlist[i * n + j + 1];
			white_noise_qlist[index]->verts[2] = white_noise_vlist[(i + 1) * n + j + 1];
			white_noise_qlist[index]->verts[3] = white_noise_vlist[(i + 1) * n + j];
		}
	}
}

double getScalar(double x, double y) {
	//find the quad it is in
	Quad *q = NULL;
	double x1, x2, y1, y2;
	for (int i = 0; i < white_noise_nquads; i++) {
		q = white_noise_qlist[i];

		x1 = x2 = q->verts[0]->x;
		y1 = y2 = q->verts[0]->y;

		//similar to how did we find the x1,x2,y1,y2 in hw2
		for (int j = 0; j < 4; j++) {

			// x1 is the smallest x coordinate of the 4 verts
			if (q->verts[j]->x < x1) {
				x1 = q->verts[j]->x;
			}

			// x2 is the biggest x coordinate of the 4 verts
			if (q->verts[j]->x > x2) {
				x2 = q->verts[j]->x;
			}

			// y1 is the smallest y coordinate of the 4 verts
			if (q->verts[j]->y < y1) {
				y1 = q->verts[j]->y;
			}

			// y2 is the biggest y coordinate of the 4 verts
			if (q->verts[j]->y > y2) {
				y2 = q->verts[j]->y;
			}
		}

		if (x <= x2 && x >= x1 && y <= y2 && y >= y1) {
			break;
		}
	}

	//find the k, which corresponds to the vertex with x1 and y1,
	int k;
	for (int i = 0; i < 4; i++) {
		if (q->verts[i]->x == x1 && q->verts[i]->y == y1) {
			k = i;
		}
	}

	double fx1y1 = q->verts[k]->scalar;
	double fx2y1 = q->verts[(k+1)%4]->scalar;
	double fx2y2 = q->verts[(k+2)%4]->scalar;
	double fx1y2 = q->verts[(k+3)%4]->scalar;

	double fx0y0 = ((x2-x)/(x2-x1))*((y2-y)/(y2-y1))*fx1y1 +\
					((x-x1)/(x2-x1))*((y2-y)/(y2-y1))*fx2y1 +\
					((x2-x)/(x2-x1))*((y-y1)/(y2-y1))*fx1y2 +\
					((x-x1)/(x2-x1))*((y-y1)/(y2-y1))*fx2y2;
	
	return fx0y0;
}

double kernel(double x) {
	// Gaussian Kernel
	return 1 / sqrt(2 * PI) * exp(-1/2 * x*x);
}

double weighted_average(std::vector<double> scalars) {
	double step = 2 * 1.96 / (scalars.size()-1);
	double x = -1.96;
	double weighted_sum = 0;
	double total_weights = 0;

	for (int i = 0; i < scalars.size(); i++) {
		weighted_sum += kernel(x) * scalars[i];
		total_weights += kernel(x);
		x += step;
	}

	return weighted_sum / total_weights;
}

double extract_line_integral(double x, double y, double z) {
	double step = 0.01;		// test the effect of different step
	int count = 200;		// the times of tracing
	double c_x = x;			// save the start position
	double c_y = y;
	double c_z = z;

	std::vector<double> scalars;
	scalars.push_back(getScalar(c_x, c_y));

	//tracing forward
	for (int i = 0; i < count; i++) {
		
		//1. check whether cx, cy, cz inside the mesh, you need to calculate the maximum and minimum of the x and y coordinate first
		if (c_x > poly->maxx || c_x < poly->minx || c_y > poly->maxy || c_y < poly->miny)
			break;

		//2. get the dir at cx, cy, cz
		icVector3 start = icVector3(c_x, c_y, c_z);
		icVector3 dir = getDir(c_x, c_y, c_z);		//write your own function to get the direction at location (x, y, z)
		normalize(dir);
		icVector3 end = start + dir * step;

		//3. update the cx, cy, cz
		c_x = end.x;
		c_y = end.y;
		c_z = end.z;

		//4. check whether cx, cy, cz inside the mesh, if not then create the lineSegment and save it in contour with using below codes
		if (c_x > poly->maxx || c_x < poly->minx || c_y > poly->maxy || c_y < poly->miny)
			break;

		scalars.push_back(getScalar(end.x, end.y));
	}

	// reset the start position
	c_x = x;
	c_y = y;
	c_z = z;

	//tracing backward
	for (int i = 0; i < count; i++) {
		// same as tracing forward, except that the direction is reversed.
		//1. check whether cx, cy, cz inside the mesh, you need to calculate the maximum and minimum of the x and y coordinate first
		if (c_x > poly->maxx || c_x < poly->minx || c_y > poly->maxy || c_y < poly->miny)
			break;

		//2. get the dir at cx, cy, cz
		icVector3 start = icVector3(c_x, c_y, c_z);
		icVector3 dir = -getDir(c_x, c_y, c_z);		//write your own function to get the direction at location (x, y, z)
		normalize(dir);
		icVector3 end = start + dir * step;

		//3. update the cx, cy, cz
		c_x = end.x;
		c_y = end.y;
		c_z = end.z;

		//4. check whether cx, cy, cz inside the mesh, if not then create the lineSegment and save it in contour with using below codes
		if (c_x > poly->maxx || c_x < poly->minx || c_y > poly->maxy || c_y < poly->miny)
			break;
			
		scalars.insert(scalars.begin(), getScalar(end.x, end.y));
	}

	return weighted_average(scalars);
}

void extract_streamline(double x, double y, double z, PolyLine &contour) {

	double step = 0.01;		// test the effect of different step
	int count = 250;		// the times of tracing
	double c_x = x;			// save the start position
	double c_y = y;
	double c_z = z;

	//tracing forward
	for (int i = 0; i < count; i++) {
		
		//1. check whether cx, cy, cz inside the mesh, you need to calculate the maximum and minimum of the x and y coordinate first
		if (c_x > poly->maxx || c_x < poly->minx || c_y > poly->maxy || c_y < poly->miny)
			break;

		//2. get the dir at cx, cy, cz
		icVector3 start = icVector3(c_x, c_y, c_z);
		icVector3 dir = getDir(c_x, c_y, c_z);		//write your own function to get the direction at location (x, y, z)
		normalize(dir);
		icVector3 end = start + dir * step;

		//3. update the cx, cy, cz
		c_x = end.x;
		c_y = end.y;
		c_z = end.z;

		//4. check whether cx, cy, cz inside the mesh, if not then create the lineSegment and save it in contour with using below codes
		if (c_x > poly->maxx || c_x < poly->minx || c_y > poly->maxy || c_y < poly->miny)
			break;

		LineSegment line = LineSegment(start, end);
		contour.push_back(line);
	}

	// reset the start position
	c_x = x;
	c_y = y;
	c_z = z;

	//tracing backward
	for (int i = 0; i < count; i++) {
		// same as tracing forward, except that the direction is reversed.
		//1. check whether cx, cy, cz inside the mesh, you need to calculate the maximum and minimum of the x and y coordinate first
		if (c_x > poly->maxx || c_x < poly->minx || c_y > poly->maxy || c_y < poly->miny)
			break;

		//2. get the dir at cx, cy, cz
		icVector3 start = icVector3(c_x, c_y, c_z);
		icVector3 dir = -getDir(c_x, c_y, c_z);		//write your own function to get the direction at location (x, y, z)
		normalize(dir);
		icVector3 end = start + dir * step;

		//3. update the cx, cy, cz
		c_x = end.x;
		c_y = end.y;
		c_z = end.z;

		//4. check whether cx, cy, cz inside the mesh, if not then create the lineSegment and save it in contour with using below codes
		if (c_x > poly->maxx || c_x < poly->minx || c_y > poly->maxy || c_y < poly->miny)
			break;
			
		LineSegment line = LineSegment(start, end);
		contour.push_back(line);
	}
}

std::vector<double> quadratic_formula(double a, double b, double c) {
	std::vector<double> s;
	double discriminant = b*b - 4*a*c;
	if (discriminant > 0) {
		double s1 = (-b + sqrt(discriminant)) / (2*a);
		double s2 = (-b - sqrt(discriminant)) / (2*a);

		if (s1 > 0 && s1 < 1)
			s.push_back(s1);
		if (s2 > 0 && s2 < 1)
			s.push_back(s2);

	} else if (discriminant == 0) {
		double s1 = -b / (2*a);
		if (s1 > 0 && s1 < 1)
			s.push_back(s1);
	}

	return s;
}

void extract_quad_singularity(Quad *q) {

	// clear the current results
	if (q->critical)
		delete(q->critical);
	q->critical = NULL;

	double x1, x2, y1, y2;
	x1 = x2 = q->verts[0]->x;
	y1 = y2 = q->verts[0]->y;

	//find x1 x2 y1 y2 by comparing vertices' x and y
	for (int i = 0; i < 4; i++) {

		// x1 is the smallest x coordinate of the 4 verts
		if (q->verts[i]->x < x1) {
			x1 = q->verts[i]->x;
		}

		// x2 is the biggest x coordinate of the 4 verts
		if (q->verts[i]->x > x2) {
			x2 = q->verts[i]->x;
		}

		// y1 is the smallest y coordinate of the 4 verts
		if (q->verts[i]->y < y1) {
			y1 = q->verts[i]->y;
		}

		// y2 is the biggest y coordinate of the 4 verts
		if (q->verts[i]->y > y2) {
			y2 = q->verts[i]->y;
		}
	}

	//find the k to make q->vert[k] has coordinate x1 and y1
	//which are the smallest x and y coordinate of verts
	//inside the quad q.
	int k;
	for (int i = 0; i < 4; i++) {
		if (q->verts[i]->x == x1 && q->verts[i]->y == y1) {
			k = i;
		}
	}

	double fx1y1 = q->verts[k]->vx;					//f(x, y) is the vx
	double fx2y1 = q->verts[(k + 1) % 4]->vx;
	double fx2y2 = q->verts[(k + 2) % 4]->vx;
	double fx1y2 = q->verts[(k + 3) % 4]->vx;

	double gx1y1 = q->verts[k]->vy;					//g(x, y) is the vy
	double gx2y1 = q->verts[(k + 1) % 4]->vy;
	double gx2y2 = q->verts[(k + 2) % 4]->vy;
	double gx1y2 = q->verts[(k + 3) % 4]->vy;

	//solve s1 and s2
	double a00 = fx1y1;
	double a10 = fx2y1 - fx1y1;
	double a01 = fx1y2 - fx1y1;
	double a11 = fx1y1 - fx2y1 - fx1y2 + fx2y2;

	double b00 = gx1y1;
	double b10 = gx2y1 - gx1y1;
	double b01 = gx1y2 - gx1y1;
	double b11 = gx1y1 - gx2y1 - gx1y2 + gx2y2;

	double c00 = a11*b00 - a00*b11;
	double c10 = a11*b10 - a10*b11;
	double c01 = a11*b01 - a01*b11;

	double a = -a11*c10;
	double b = -a11*c00 - a01*c10 + a10*c01;
	double c = a00*c01 - a01*c00;

	//solve s1 and s2
	std::vector<double> roots = quadratic_formula(a, b, c);

	//find valid t
	double s = NULL;
	double t = NULL;
	for (int i = 0; i < roots.size(); i++) {
		double s1 = roots[i];
		double t1 = -c00/c01 - c10/c01*s1;
		if (t1 > 0 && t1 < 1) {
			s = s1;
			t = t1;
			break;
		}
	}
	
	if (s != NULL && t != NULL) {
		
		double x0 = (x2 - x1)*s + x1;	//linear interpolate between x1 and x2 with s to get x0
		double y0 = (y2 - y1)*t + y1;	//linear interpolate between y1 and y2 with t to get y0

		double df_dx = (-1/(x2-x1))*((y2-y0)/(y2-y1))*fx1y1 +\
					(1/(x2-x1))*((y2-y0)/(y2-y1))*fx2y1 +\
					(-1/(x2-x1))*((y0-y1)/(y2-y1))*fx1y2 +\
					(1/(x2-x1))*((y0-y1)/(y2-y1))*fx2y2;
		double df_dy = ((x2-x0)/(x2-x1))*(-1/(y2-y1))*fx1y1 +\
					((x0-x1)/(x2-x1))*(-1/(y2-y1))*fx2y1 +\
					((x2-x0)/(x2-x1))*(1/(y2-y1))*fx1y2 +\
					((x0-x1)/(x2-x1))*(1/(y2-y1))*fx2y2;
		double dg_dx = (-1/(x2-x1))*((y2-y0)/(y2-y1))*gx1y1 +\
					(1/(x2-x1))*((y2-y0)/(y2-y1))*gx2y1 +\
					(-1/(x2-x1))*((y0-y1)/(y2-y1))*gx1y2 +\
					(1/(x2-x1))*((y0-y1)/(y2-y1))*gx2y2;
		double dg_dy = ((x2-x0)/(x2-x1))*(-1/(y2-y1))*gx1y1 +\
					((x0-x1)/(x2-x1))*(-1/(y2-y1))*gx2y1 +\
					((x2-x0)/(x2-x1))*(1/(y2-y1))*gx1y2 +\
					((x0-x1)/(x2-x1))*(1/(y2-y1))*gx2y2;
		double det = df_dx*dg_dy - df_dy*dg_dx;

		Vertex *sing = new Vertex(x0, y0, 0);

		//modify the Quad class to save singularity
		q->singularity = sing;
		if (det < 0) {
			q->singularity_type = -1;	//saddles
		} else if (det == 0) {
			q->singularity_type = 0;	//higher-order singularities (dipoles, monkey saddles)
		} else {
			q->singularity_type = 1;	//everything else (source, sink, center, focus)
		}
	}
}

void extract_singularities() {
	for (int i = 0; i < poly->nquads; i++) {
		Quad *q = poly->qlist[i];
		extract_quad_singularity(q);
	}
}

void keyboard(unsigned char key, int x, int y) {
	int i;

	/* set escape key to exit */
	switch (key) {
	case 27:
		poly->finalize();  // finalize_everything
		exit(0);
		break;

	case '1':
		display_mode = 1;
		for (int i = 0; i < poly->nverts; i++) {
			Vertex* temp_v = poly->vlist[i];
			temp_v->z = 0;
		}
		glutPostRedisplay();
		break;

	case '2':
	{
		display_mode = 2;
		compute_white_noise();

		for (int i = 0; i < white_noise_nverts; i++) {
			double x, y, z;
			x = white_noise_vlist[i]->x;
			y = white_noise_vlist[i]->y;
			z = white_noise_vlist[i]->z;
			white_noise_vlist[i]->output = extract_line_integral(x, y, z);
		}

		//calculate the m and M
		double min = white_noise_vlist[0]->output;	//m
		double max = white_noise_vlist[0]->output;	//M
		for (int i = 0; i < white_noise_nverts; i++) {
			
			Vertex* temp_v = white_noise_vlist[i];
			double output = temp_v->output;

			if (output < min) {
				min = output;
			}

			if (output > max) {
				max = output;
			}
		}

		//update the colors of each vertex
		for (int i = 0; i < white_noise_nverts; i++) {

			Vertex* temp_v = white_noise_vlist[i];
			double output = temp_v->output;		//f(v)

			temp_v->R = (output - min) / (max - min);
			temp_v->G = (output - min) / (max - min);
			temp_v->B = (output - min) / (max - min);

			//reset height
			temp_v->z = 0;
		}
		glutPostRedisplay();
	}
	break;

	case '3':
	{
		display_mode = 3;

		for (int i = 0; i < poly->nverts; i++) {
			Vertex* temp_v = poly->vlist[i];
			temp_v->z = 0;
		}

		double L = (poly->radius * 2) / 30;
		for (int i = 0; i < poly->nquads; i++) {
			Quad* temp_q = poly->qlist[i];
			for (int j = 0; j < 4; j++) {

				Vertex* temp_v = temp_q->verts[j];

				temp_v->R = int(temp_v->x / L) % 2 == 0 ? 1 : 0;
				temp_v->G = int(temp_v->y / L) % 2 == 0 ? 1 : 0;
				temp_v->B = 0.0;
			}
		}
		glutPostRedisplay();
	}
	break;
	case '4':
		display_mode = 4;
		{
			for (int i = 0; i < poly->nverts; i++) {
				Vertex* temp_v = poly->vlist[i];
				temp_v->z = 0;
			}
			//examples for dot drawing and polyline drawing

			//create a polylines of a pentagon
			//clear current polylines
			pentagon.clear();
			//there are five vertices of a pentagon
			//the angle of each edge is 2pi/5.0
			double da = 2.0*PI / 5.0;
			for (int i = 0; i < 5; i++) {
				double angle = i * da;
				double cx = cos(angle);
				double cy = sin(angle);

				double n_angle = (i + 1) % 5 * da;
				double nx = cos(n_angle);
				double ny = sin(n_angle);

				LineSegment line(cx, cy, 0, nx, ny, 0);
				pentagon.push_back(line);
			}

		}
		glutPostRedisplay();
		break;

	case '5':
	{
		display_mode = 5;
		for (int i = 0; i < poly->nverts; i++) {
			Vertex* temp_v = poly->vlist[i];
			temp_v->z = 0;
		}
		//show the IBFV of the field
	}
	break;

	case '6':
	{
		display_mode = 6;

		for (int i = 0; i < poly->nverts; i++) {
			Vertex* temp_v = poly->vlist[i];
			temp_v->z = 0;
		}

		//calculate the m and M
		min = poly->vlist[0]->scalar;	//m
		max = poly->vlist[0]->scalar;	//M
		for (int i = 0; i < poly->nverts; i++) {
			
			Vertex* temp_v = poly->vlist[i];
			double scalar = temp_v->scalar;

			if (scalar < min) {
				min = scalar;
			}

			if (scalar > max) {
				max = scalar;
			}
		}

		int n = 50;
		double step = (max - min)/(n-1);

		contours.clear();
		for (double s = min; s <= max; s += step) {
			for (int i = 0; i < poly->nverts; i++) {
				Vertex *v = poly->vlist[i];
				double scalar = v->scalar;

				if (scalar < s) {
					v->vertex_type = 0;
				} else {
					v->vertex_type = 1;
				}
			}

			for (int i = 0; i < poly->nedges; i++) {
				Edge* temp_e = poly->elist[i];
				Vertex *v1 = temp_e->verts[0];
				Vertex *v2 = temp_e->verts[1];
				if (v1->vertex_type != v2->vertex_type) {
					temp_e->edge_type = 1;
					double alpha = (s - v1->scalar) / (v2->scalar - v1->scalar);
					double v_x = alpha * (v2->x - v1->x) + v1->x;
					double v_y = alpha * (v2->y - v1->y) + v1->y;
					double v_z = alpha * (v2->y - v1->y) + v1->z;
					temp_e->crossing = new Vertex(v_x, v_y, v_z);
				} else {
					temp_e->edge_type = 0;
					temp_e->crossing = NULL;
				}
			}

			contour_s.clear();
			//loop through quads
			for (int i = 0; i < poly->nquads; i++) {
				Quad *q = poly->qlist[i];

				//find the saved crossing point on each edge
				crossings.clear();
				for (int j = 0; j < 4; j++) {
					Edge* e = q->edges[j];
					if (e->edge_type == 1) {
						crossings.push_back(e->crossing);
					}
				}
				
				if (crossings.size() == 2) {
					Vertex* v1 = crossings[0];
					Vertex* v2 = crossings[1];
					LineSegment line(v1->x, v1->y, v1->z, v2->x, v2->y, v2->z);
					contour_s.push_back(line);
				} else if (crossings.size() == 4) {
					Vertex* v1 = crossings[2];
					Vertex* v2 = crossings[3];
					LineSegment line(v1->x, v1->y, v1->z, v2->x, v2->y, v2->z);
					contour_s.push_back(line);
				}
			}
			contours.push_back(std::pair<PolyLine, float>(contour_s, s));
		}

		glutPostRedisplay();
		break;
	}

	case '7':
	{
		display_mode = 7;

		for (int i = 0; i < poly->nverts; i++) {
			Vertex* temp_v = poly->vlist[i];
			temp_v->z = 0;
		}

		//calculate the m and M
		min = poly->vlist[0]->scalar;	//m
		max = poly->vlist[0]->scalar;	//M
		for (int i = 0; i < poly->nverts; i++) {
			
			Vertex* temp_v = poly->vlist[i];
			double scalar = temp_v->scalar;

			if (scalar < min) {
				min = scalar;
			}

			if (scalar > max) {
				max = scalar;
			}
		}

		int n = 50;
		double step = (max - min)/(n-1);

		contours.clear();
		for (double s = min; s <= max; s += step) {
			for (int i = 0; i < poly->nverts; i++) {
				Vertex *v = poly->vlist[i];
				double scalar = v->scalar;

				if (scalar < s) {
					v->vertex_type = 0;
				} else {
					v->vertex_type = 1;
				}
			}

			for (int i = 0; i < poly->nedges; i++) {
				Edge* temp_e = poly->elist[i];
				Vertex *v1 = temp_e->verts[0];
				Vertex *v2 = temp_e->verts[1];
				if (v1->vertex_type != v2->vertex_type) {
					temp_e->edge_type = 1;
					double alpha = (s - v1->scalar) / (v2->scalar - v1->scalar);
					double v_x = alpha * (v2->x - v1->x) + v1->x;
					double v_y = alpha * (v2->y - v1->y) + v1->y;
					double v_z = alpha * (v2->y - v1->y) + v1->z;
					temp_e->crossing = new Vertex(v_x, v_y, v_z);
				} else {
					temp_e->edge_type = 0;
					temp_e->crossing = NULL;
				}
			}

			contour_s.clear();
			//loop through quads
			for (int i = 0; i < poly->nquads; i++) {
				Quad *q = poly->qlist[i];

				//find the saved crossing point on each edge
				crossings.clear();
				for (int j = 0; j < 4; j++) {
					Edge* e = q->edges[j];
					if (e->edge_type == 1) {
						crossings.push_back(e->crossing);
					}
				}
				
				if (crossings.size() == 2) {
					Vertex* v1 = crossings[0];
					Vertex* v2 = crossings[1];
					LineSegment line(v1->x, v1->y, v1->z, v2->x, v2->y, v2->z);
					contour_s.push_back(line);
				} else if (crossings.size() == 4) {
					Vertex* v1 = crossings[2];
					Vertex* v2 = crossings[3];
					LineSegment line(v1->x, v1->y, v1->z, v2->x, v2->y, v2->z);
					contour_s.push_back(line);
				}
			}
			contours.push_back(std::pair<PolyLine, float>(contour_s, s));
		}

		glutPostRedisplay();
		break;
	}

	case '8':
	{
		display_mode = 8;

		//calculate the m and M
		min = poly->vlist[0]->scalar;	//m
		max = poly->vlist[0]->scalar;	//M
		for (int i = 0; i < poly->nverts; i++) {
			
			Vertex* temp_v = poly->vlist[i];
			double scalar = temp_v->scalar;

			if (scalar < min) {
				min = scalar;
			}

			if (scalar > max) {
				max = scalar;
			}
		}

		double a = 0;
		double b = 20;
		for (int i = 0; i < poly->nverts; i++) {
			Vertex* temp_v = poly->vlist[i];
			double scalar = temp_v->scalar;
			temp_v->z = (b-a)*(scalar - min)/(max-min) + a;
		}

		int n = 50;
		double step = (max - min)/(n-1);

		contours.clear();
		for (double s = min; s <= max; s += step) {
			for (int i = 0; i < poly->nverts; i++) {
				Vertex *v = poly->vlist[i];
				double scalar = v->scalar;

				if (scalar < s) {
					v->vertex_type = 0;
				} else {
					v->vertex_type = 1;
				}
			}

			for (int i = 0; i < poly->nedges; i++) {
				Edge* temp_e = poly->elist[i];
				Vertex *v1 = temp_e->verts[0];
				Vertex *v2 = temp_e->verts[1];
				if (v1->vertex_type != v2->vertex_type) {
					temp_e->edge_type = 1;
					double alpha = (s - v1->scalar) / (v2->scalar - v1->scalar);
					double v_x = alpha * (v2->x - v1->x) + v1->x;
					double v_y = alpha * (v2->y - v1->y) + v1->y;
					double v_z = alpha * (v2->y - v1->y) + v1->z;
					temp_e->crossing = new Vertex(v_x, v_y, v_z);
				} else {
					temp_e->edge_type = 0;
					temp_e->crossing = NULL;
				}
			}

			contour_s.clear();
			//loop through quads
			for (int i = 0; i < poly->nquads; i++) {
				Quad *q = poly->qlist[i];

				//find the saved crossing point on each edge
				crossings.clear();
				for (int j = 0; j < 4; j++) {
					Edge* e = q->edges[j];
					if (e->edge_type == 1) {
						crossings.push_back(e->crossing);
					}
				}
				
				if (crossings.size() == 2) {
					Vertex* v1 = crossings[0];
					Vertex* v2 = crossings[1];
					LineSegment line(v1->x, v1->y, v1->z, v2->x, v2->y, v2->z);
					contour_s.push_back(line);
				} else if (crossings.size() == 4) {
					Vertex* v1 = crossings[2];
					Vertex* v2 = crossings[3];
					LineSegment line(v1->x, v1->y, v1->z, v2->x, v2->y, v2->z);
					contour_s.push_back(line);
				}
			}
			contours.push_back(std::pair<PolyLine, float>(contour_s, s));
		}

		glutPostRedisplay();
		break;
	}

	case '9':
	{
		display_mode = 9;

		//calculate the m and M
		min = poly->vlist[0]->scalar;	//m
		max = poly->vlist[0]->scalar;	//M
		for (int i = 0; i < poly->nverts; i++) {
			
			Vertex* temp_v = poly->vlist[i];
			double scalar = temp_v->scalar;

			if (scalar < min) {
				min = scalar;
			}

			if (scalar > max) {
				max = scalar;
			}
		}

		double a = 0;
		double b = 20;
		for (int i = 0; i < poly->nverts; i++) {
			Vertex* temp_v = poly->vlist[i];
			double scalar = temp_v->scalar;
			temp_v->z = (b-a)*(scalar - min)/(max-min) + a;
		}

		int n = 50;
		double step = (max - min)/(n-1);

		contours.clear();
		for (double s = min; s <= max; s += step) {
			for (int i = 0; i < poly->nverts; i++) {
				Vertex *v = poly->vlist[i];
				double scalar = v->scalar;

				if (scalar < s) {
					v->vertex_type = 0;
				} else {
					v->vertex_type = 1;
				}
			}

			for (int i = 0; i < poly->nedges; i++) {
				Edge* temp_e = poly->elist[i];
				Vertex *v1 = temp_e->verts[0];
				Vertex *v2 = temp_e->verts[1];
				if (v1->vertex_type != v2->vertex_type) {
					temp_e->edge_type = 1;
					double alpha = (s - v1->scalar) / (v2->scalar - v1->scalar);
					double v_x = alpha * (v2->x - v1->x) + v1->x;
					double v_y = alpha * (v2->y - v1->y) + v1->y;
					double v_z = alpha * (v2->y - v1->y) + v1->z;
					temp_e->crossing = new Vertex(v_x, v_y, v_z);
				} else {
					temp_e->edge_type = 0;
					temp_e->crossing = NULL;
				}
			}

			contour_s.clear();
			//loop through quads
			for (int i = 0; i < poly->nquads; i++) {
				Quad *q = poly->qlist[i];

				//find the saved crossing point on each edge
				crossings.clear();
				for (int j = 0; j < 4; j++) {
					Edge* e = q->edges[j];
					if (e->edge_type == 1) {
						crossings.push_back(e->crossing);
					}
				}
				
				if (crossings.size() == 2) {
					Vertex* v1 = crossings[0];
					Vertex* v2 = crossings[1];
					LineSegment line(v1->x, v1->y, v1->z, v2->x, v2->y, v2->z);
					contour_s.push_back(line);
				} else if (crossings.size() == 4) {
					Vertex* v1 = crossings[2];
					Vertex* v2 = crossings[3];
					LineSegment line(v1->x, v1->y, v1->z, v2->x, v2->y, v2->z);
					contour_s.push_back(line);
				}
			}
			contours.push_back(std::pair<PolyLine, float>(contour_s, s));
		}

		//loop through quads to find
		//whether there is a critical
		for (int i = 0; i < poly->nquads; i++) {

			Quad* q = poly->qlist[i];

			// 1. clear exited calculated critical
			if (q->critical) {
				delete(q->critical);
			}
			q->critical = NULL;

			// 2. find x1, x2, y1, y2
			double x1 = q->verts[0]->x;
			double x2 = q->verts[0]->x;
			double y1 = q->verts[0]->y;
			double y2 = q->verts[0]->y;
			for (int j = 0; j < 4; j++) {

				// x1 is the smallest x coordinate of the 4 verts
				if (q->verts[j]->x < x1) {
					x1 = q->verts[j]->x;
				}

				// x2 is the biggest x coordinate of the 4 verts
				if (q->verts[j]->x > x2) {
					x2 = q->verts[j]->x;
				}

				// y1 is the smallest y coordinate of the 4 verts
				if (q->verts[j]->y < y1) {
					y1 = q->verts[j]->y;
				}

				// y2 is the biggest y coordinate of the 4 verts
				if (q->verts[j]->y > y2) {
					y2 = q->verts[j]->y;
				}
			}

			// 3. find fx1y1, fx2y1, fx2y2, fx1y2
			int k;
			for (int j = 0; j < 4; j++) {
				if (q->verts[j]->x == x1 && q->verts[j]->y == y1) {
					k = j;
				}
			}
			double fx1y1 = q->verts[k]->scalar;
			double fx2y1 = q->verts[(k+1)%4]->scalar;
			double fx2y2 = q->verts[(k+2)%4]->scalar;
			double fx1y2 = q->verts[(k+3)%4]->scalar;

			// 4. calculate the x0, y0
			double x0 = (x2*fx1y1 - x1*fx2y1 - x2*fx1y2 + x1*fx2y2) / (fx1y1 - fx2y1 - fx1y2 + fx2y2);
			double y0 = (y2*fx1y1 - y2*fx2y1 - y1*fx1y2 + y1*fx2y2) / (fx1y1 - fx2y1 - fx1y2 + fx2y2);

			// 5. check whether the x0 and y0 is inside the quad. Note that
			// the x0 y0 calculated are invalid when (fx1y1 - fx2y1 - fx1y2 + fx2y2) == 0;
			int valid = 1;
			if (x0 < x1 || x0 > x2 || y0 < y1 || y0 > y2) {
				valid = 0;
			}

			// 6. save the calculated critical point and calculate its scalar value
			if (valid) {
				double fx0y0 = ((x2-x0)/(x2-x1))*((y2-y0)/(y2-y1))*fx1y1 +\
								((x0-x1)/(x2-x1))*((y2-y0)/(y2-y1))*fx2y1 +\
								((x2-x0)/(x2-x1))*((y0-y1)/(y2-y1))*fx1y2 +\
								((x0-x1)/(x2-x1))*((y0-y1)/(y2-y1))*fx2y2;
				double z0 = (b-a)*(fx0y0 - min)/(max-min) + a;
				Vertex *critical = new Vertex(x0, y0, z0);
				critical->scalar = fx0y0;
				q->critical = critical;
			}
		}

		for (int i = 0; i < poly->nquads; i++) {
			Quad* q = poly->qlist[i];
			if (q->critical) {
				scalars.push_back(q->critical->scalar);
			}
		}

		for (int i = 0; i < scalars.size(); i++) {
			double s = scalars[i];
			for (int i = 0; i < poly->nverts; i++) {
				Vertex *v = poly->vlist[i];
				double scalar = v->scalar;

				if (scalar < s) {
					v->vertex_type = 0;
				} else {
					v->vertex_type = 1;
				}
			}

			for (int i = 0; i < poly->nedges; i++) {
				Edge* temp_e = poly->elist[i];
				Vertex *v1 = temp_e->verts[0];
				Vertex *v2 = temp_e->verts[1];
				if (v1->vertex_type != v2->vertex_type) {
					temp_e->edge_type = 1;
					double alpha = (s - v1->scalar) / (v2->scalar - v1->scalar);
					double v_x = alpha * (v2->x - v1->x) + v1->x;
					double v_y = alpha * (v2->y - v1->y) + v1->y;
					double v_z = alpha * (v2->y - v1->y) + v1->z;
					temp_e->crossing = new Vertex(v_x, v_y, v_z);
				} else {
					temp_e->edge_type = 0;
					temp_e->crossing = NULL;
				}
			}

			contour_s.clear();
			//loop through quads
			for (int i = 0; i < poly->nquads; i++) {
				Quad *q = poly->qlist[i];

				//find the saved crossing point on each edge
				crossings.clear();
				for (int j = 0; j < 4; j++) {
					Edge* e = q->edges[j];
					if (e->edge_type == 1) {
						crossings.push_back(e->crossing);
					}
				}
				
				if (crossings.size() == 2) {
					Vertex* v1 = crossings[0];
					Vertex* v2 = crossings[1];
					LineSegment line(v1->x, v1->y, v1->z, v2->x, v2->y, v2->z);
					contour_s.push_back(line);
				} else if (crossings.size() == 4) {
					Vertex* v1 = crossings[2];
					Vertex* v2 = crossings[3];
					LineSegment line(v1->x, v1->y, v1->z, v2->x, v2->y, v2->z);
					contour_s.push_back(line);
				}
			}
			contours.push_back(std::pair<PolyLine, float>(contour_s, s));
		}

		glutPostRedisplay();
		break;
	}

	case 't':
	{
		for (int i = 0; i < poly->nverts; i++) {
			if (i == 0) {
				//create minx, miny, maxx, maxy variables in Polyhedron to save the dimension
				poly->minx = poly->vlist[i]->x;
				poly->maxx = poly->vlist[i]->x;
				poly->miny = poly->vlist[i]->y;
				poly->maxy = poly->vlist[i]->y;
			} else {
				if (poly->vlist[i]->x < poly->minx)
					poly->minx = poly->vlist[i]->x;
				if (poly->vlist[i]->x > poly->maxx)
					poly->maxx = poly->vlist[i]->x;
				if (poly->vlist[i]->y < poly->miny)
					poly->miny = poly->vlist[i]->y;
				if (poly->vlist[i]->y > poly->maxy)
					poly->maxy = poly->vlist[i]->y;
			}
		}
		
		streamlines.clear();

		for (int i = 0; i < poly->nverts; i++) {
			double x, y, z;
			x = poly->vlist[i]->x;
			y = poly->vlist[i]->y;
			z = poly->vlist[i]->z;
			PolyLine s1;
			extract_streamline(x, y, z, s1);
			streamlines.push_back(s1);
		}

		glutPostRedisplay();
		break;
	}

	case 'e':
	{
		//flip the drawing flag
		showPickedPoint = !showPickedPoint;

		if (showPickedPoint) {
			/*get the dimension*/
			for (int i = 0; i < poly->nverts; i++) {
				if (i == 0) {
					//create minx, miny, maxx, maxy variables in Polyhedron to save the dimension
					poly->minx = poly->vlist[i]->x;
					poly->maxx = poly->vlist[i]->x;
					poly->miny = poly->vlist[i]->y;
					poly->maxy = poly->vlist[i]->y;
				} else {
					if (poly->vlist[i]->x < poly->minx)
						poly->minx = poly->vlist[i]->x;
					if (poly->vlist[i]->x > poly->maxx)
						poly->maxx = poly->vlist[i]->x;
					if (poly->vlist[i]->y < poly->miny)
						poly->miny = poly->vlist[i]->y;
					if (poly->vlist[i]->y > poly->maxy)
						poly->maxy = poly->vlist[i]->y;
				}
			}
			printf("The x coordinate of mesh ranges [%.3f,%.3f]\n", poly->minx, poly->maxx);
			printf("The y coordinate of mesh ranges [%.3f,%.3f]\n", poly->miny, poly->maxy);

			/*type the picked point*/
			float input_x, input_y;
			bool valid_input_x = false;
			bool valid_input_y = false;

			std::cout << "Please input the location" << std::endl;
			while (!valid_input_x) {
				std::cout << "please input the x:" << std::endl;
				std::cin >> input_x;
				if (input_x <= poly->maxx && input_x >= poly->minx) {
					valid_input_x = true;
				}
			}
			while (!valid_input_y) {
				std::cout << "please input the y:" << std::endl;
				std::cin >> input_y;
				if (input_y <= poly->maxy && input_y >= poly->miny) {
					valid_input_y = true;
				}
			}

			pickedPoint.set(input_x, input_y, 0);
			printf("Picked point locates (%.4f, %.4f)\n", pickedPoint.x, pickedPoint.y);
			
			streamline_picked.clear();
			extract_streamline(pickedPoint.x, pickedPoint.y, 0, streamline_picked);
		
		} else {
			streamline_picked.clear();
		}
	}
	break;

	case 'r':
		mat_ident(rotmat);
		translation[0] = 0;
		translation[1] = 0;
		zoom = 1.0;
		glutPostRedisplay();
		break;
	}
}

/******************************************************************************
Diaplay the polygon with visualization results
******************************************************************************/


void display_polyhedron(Polyhedron* poly)
{
	glEnable(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(1., 1.);

	glEnable(GL_DEPTH_TEST);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glShadeModel(GL_SMOOTH);
	CHECK_GL_ERROR();

	switch (display_mode) {
	case 1:
	{
		//display the mesh with color cyan (0.0, 1.0, 1.0)
		glDisable(GL_LIGHTING);
		for (int i = 0; i < poly->nquads; i++) {
			Quad* temp_q = poly->qlist[i];
			glBegin(GL_POLYGON);
			for (int j = 0; j < 4; j++) {
				Vertex* temp_v = temp_q->verts[j];
				glColor3f(0.0, 1.0, 1.0);
				glVertex3d(temp_v->x, temp_v->y, temp_v->z);
			}
			glEnd();
		}

		if (showPickedPoint) {
			drawDot(pickedPoint.x, pickedPoint.y, pickedPoint.z, 0.2, 1.0, 0.5, 0.0);
			drawPolyline(streamline_picked, 2.0, 1.0, 0.0, 0.0);
		}

		//display critical points
		extract_singularities();
		for (int i = 0; i < poly->nquads; i++) {
			Quad *q = poly->qlist[i];
			if (q->singularity == NULL)
				continue;
			
			double x = q->singularity->x;
			double y = q->singularity->y;
			double z = q->singularity->z;

			if (q->singularity_type == -1) {
				//saddle = blue
				drawDot(x, y, z, 0.2, 0.0, 0.0, 1.0);
			} else if (q->singularity_type == 0) {
				//higher order = yellow
				drawDot(x, y, z, 0.2, 1.0, 1.0, 0.0);
			} else {
				//everything else = red
				drawDot(x, y, z, 0.2, 1.0, 0.0, 0.0);
			}
			
		}

		for (int i = 0; i < streamlines.size(); i++) {
			drawPolyline(streamlines[i], 2.0, 1.0, 0.5, 0.0);
		}
	}
	break;

	case 2:
	{
		for (int i = 0; i < white_noise_nquads; i++) {
			Quad* temp_q = white_noise_qlist[i];
			glDisable(GL_LIGHTING);
			glBegin(GL_POLYGON);
			for (int j = 0; j < 4; j++) {
				Vertex* temp_v = temp_q->verts[j];
				glColor3f(temp_v->R, temp_v->G, temp_v->B);
				glVertex3d(temp_v->x, temp_v->y, temp_v->z);
			}
			glEnd();
		}
	}
	break;

	case 3:
		glDisable(GL_LIGHTING);
		for (int i = 0; i < poly->nquads; i++) {
			Quad* temp_q = poly->qlist[i];
			glBegin(GL_POLYGON);
			for (int j = 0; j < 4; j++) {
				Vertex* temp_v = temp_q->verts[j];
				glColor3f(temp_v->R, temp_v->G, temp_v->B);
				glVertex3d(temp_v->x, temp_v->y, temp_v->z);
			}
			glEnd();
		}
		break;

	case 4:
	{
		//draw a dot at position (0.2, 0.3, 0.4) 
		//with radius 0.1 in color blue(0.0, 0.0, 1.0)
		drawDot(0.2, 0.3, 0.4, 0.1, 0.0, 0.0, 1.0);

		//draw a dot at position of vlist[110]
		//with radius 0.2 in color magenta (1.0, 0.0, 1.0)
		Vertex *v = poly->vlist[110];
		drawDot(v->x, v->y, v->z, 0.2, 1.0, 0.0, 1.0);

		//draw line segment start at vlist[110] and end at (vlist[135]->x, vlist[135]->y, 4)
		//with color (0.02, 0.1, 0.02) and width 1
		LineSegment line(poly->vlist[110]->x, poly->vlist[110]->y, poly->vlist[110]->z,
			poly->vlist[135]->x, poly->vlist[135]->y, 4);
		drawLineSegment(line, 1.0, 0.0, 1.0, 0.0);

		//draw a polyline of pentagon with color orange(1.0, 0.5, 0.0) and width 2
		drawPolyline(pentagon, 2.0, 1.0, 0.5, 0.0);

		//display the mesh with color cyan (0.0, 1.0, 1.0)
		glDisable(GL_LIGHTING);
		for (int i = 0; i < poly->nquads; i++) {
			Quad* temp_q = poly->qlist[i];
			glBegin(GL_POLYGON);
			for (int j = 0; j < 4; j++) {
				Vertex* temp_v = temp_q->verts[j];
				glColor3f(0.0, 1.0, 1.0);
				glVertex3d(temp_v->x, temp_v->y, temp_v->z);
			}
			glEnd();
		}
	}
	break;

	case 5:
		displayIBFV();
		break;

	case 6:
	{
		for (int i = 0; i < contours.size(); i++) {
			PolyLine contour_s = contours[i].first;
			double s = contours[i].second;
			drawPolyline(contour_s, 1.0, 1.0, 1.0, 0.0);
		}

		//display the mesh with color cyan (0.0, 0.0, 0.0)
		glDisable(GL_LIGHTING);
		for (int i = 0; i < poly->nquads; i++) {
			Quad* temp_q = poly->qlist[i];
			glBegin(GL_POLYGON);
			for (int j = 0; j < 4; j++) {
				Vertex* temp_v = temp_q->verts[j];
				glColor3f(0.0, 0.0, 0.0);
				glVertex3d(temp_v->x, temp_v->y, temp_v->z);
			}
			glEnd();
		}
	}
	break;

	case 7:
	{
		GLfloat c1[3] = {1, 1, 0};
		GLfloat c2[3] = {0, 0, 1};

		for (int i = 0; i < contours.size(); i++) {
			PolyLine contour_s = contours[i].first;
			double scalar = contours[i].second;
			GLfloat R = c1[0] * (scalar - min) / (max - min) + c2[0] * (max - scalar) / (max - min);
			GLfloat G = c1[1] * (scalar - min) / (max - min) + c2[1] * (max - scalar) / (max - min);
			GLfloat B = c1[2] * (scalar - min) / (max - min) + c2[2] * (max - scalar) / (max - min);
			drawPolyline(contour_s, 1.0, R, G, B);
		}

		//display the mesh with color cyan (0.0, 0.0, 0.0)
		glDisable(GL_LIGHTING);
		for (int i = 0; i < poly->nquads; i++) {
			Quad* temp_q = poly->qlist[i];
			glBegin(GL_POLYGON);
			for (int j = 0; j < 4; j++) {
				Vertex* temp_v = temp_q->verts[j];
				glColor3f(0.0, 0.0, 0.0);
				glVertex3d(temp_v->x, temp_v->y, temp_v->z);
			}
			glEnd();
		}
	}
	break;

	case 8:
	{
		GLfloat c1[3] = {1, 1, 0};
		GLfloat c2[3] = {0, 0, 1};

		for (int i = 0; i < contours.size(); i++) {
			PolyLine contour_s = contours[i].first;
			double scalar = contours[i].second;
			GLfloat R = c1[0] * (scalar - min) / (max - min) + c2[0] * (max - scalar) / (max - min);
			GLfloat G = c1[1] * (scalar - min) / (max - min) + c2[1] * (max - scalar) / (max - min);
			GLfloat B = c1[2] * (scalar - min) / (max - min) + c2[2] * (max - scalar) / (max - min);
			drawPolyline(contour_s, 1.0, R, G, B);
		}

		//display the mesh with color cyan (0.0, 0.0, 0.0)
		glDisable(GL_LIGHTING);
		for (int i = 0; i < poly->nquads; i++) {
			Quad* temp_q = poly->qlist[i];
			glBegin(GL_POLYGON);
			for (int j = 0; j < 4; j++) {
				Vertex* temp_v = temp_q->verts[j];
				glColor3f(0.0, 0.0, 0.0);
				glVertex3d(temp_v->x, temp_v->y, temp_v->z);
			}
			glEnd();
		}
	}
	break;

	case 9:
	{
		GLfloat c1[3] = {1, 1, 0};
		GLfloat c2[3] = {0, 0, 1};

		for (int i = 0; i < contours.size(); i++) {
			PolyLine contour_s = contours[i].first;
			double scalar = contours[i].second;
			GLfloat R = c1[0] * (scalar - min) / (max - min) + c2[0] * (max - scalar) / (max - min);
			GLfloat G = c1[1] * (scalar - min) / (max - min) + c2[1] * (max - scalar) / (max - min);
			GLfloat B = c1[2] * (scalar - min) / (max - min) + c2[2] * (max - scalar) / (max - min);
			drawPolyline(contour_s, 1.0, R, G, B);
		}

		for (int i = 0; i < poly->nquads; i++) {
			Quad* temp_q = poly->qlist[i];
			Vertex* crit = temp_q->critical;
			if (crit) {
				drawDot(crit->x, crit->y, crit->z, 0.2, 1.0, 1.0, 1.0);
			}
		}

		//display the mesh with color cyan (0.0, 0.0, 0.0)
		glDisable(GL_LIGHTING);
		for (int i = 0; i < poly->nquads; i++) {
			Quad* temp_q = poly->qlist[i];
			glBegin(GL_POLYGON);
			for (int j = 0; j < 4; j++) {
				Vertex* temp_v = temp_q->verts[j];
				glColor3f(0.0, 0.0, 0.0);
				glVertex3d(temp_v->x, temp_v->y, temp_v->z);
			}
			glEnd();
		}
	}
	break;
	}
}
