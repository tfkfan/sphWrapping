#include "SPHSystem.h"
#include <cmath>
#include <string>
#include <iostream>

using namespace std;

SPHSystem::SPHSystem(){
	kernel_radius = 0.04f;
	mass = 0.02f;

	maxParticle = 10000;
	numParticle = 0;

	worldSize.x = 2.56f;
	worldSize.y = 2.56f;
	cellSize = kernel_radius;
	gridSize.x = (int)(worldSize.x/cellSize);
	gridSize.y = (int)(worldSize.y/cellSize);
	totCell = (uint)(gridSize.x) * (uint)(gridSize.y);
	
	//params
	gravity.x = 0.0f;
	gravity.y = -9.8f;
	stiffness = 1000.0f;
	restDensity = 1000.0f;
	timeStep = 0.0005f;
	wallDamping = 0.0f;
	viscosity = 8.1f;

	particles = new Particle[maxParticle];
	cells = new Cell[totCell];

	cout << "SPHSystem:" << endl;
	cout << "GridSizeX:" << gridSize.x << endl;
	cout << "GridSizeY:" << gridSize.y << endl;
	cout << "TotalCell:" << totCell << endl;
}

SPHSystem::~SPHSystem(){
	free(particles);
	free(cells);
}

void SPHSystem::initFluid(int nParticles){
	Vec2f pos;
	Vec2f vel(0.0f, 0.0f);
	float x = 0 , y = worldSize.y;
	float dx = kernel_radius*0.8,dy = kernel_radius*0.8;

	maxParticle = nParticles;

	for(int i = 0; i < nParticles; i++, x+=dx){
		if(x>=worldSize.x){
			x=0;
			y-=dy;
		}
		pos.x = x;
		pos.y = y;
		addSingleParticle(pos, vel);
	}
	cout << "NUM Particle:" <<  numParticle << endl;
}

void SPHSystem::addSingleParticle(Vec2f pos, Vec2f vel){
	Particle *p = &(particles[numParticle]);
	p->pos = pos;
	p->vel = vel;
	p->acc = Vec2f(0.0f, 0.0f);
	p->ev = vel;
	p->dens = restDensity;
	p->next = NULL;
	numParticle++;
}

Vec2i SPHSystem::calcCellPos(Vec2f pos){
	Vec2i res;
	res.x = (int)(pos.x/cellSize);
	res.y = (int)(pos.y/cellSize);
	return res;
}
	
uint SPHSystem::calcCellHash(Vec2i pos){
	if(pos.x<0 || pos.x>=gridSize.x || pos.y<0 || pos.y>=gridSize.y)
		return 0xffffffff;

	uint hash = (uint)(pos.y) * (uint)(gridSize.x) + (uint)(pos.x);
	if(hash >= totCell){
		cout << "ERROR!" << endl;
		getchar();
	}
	return hash;
}

void SPHSystem::compTimeStep(){
	Particle *p;
	float maxAcc = 0.0f;
	float curAcc;
	for(uint i=0; i<numParticle; i++){
		p = &(particles[i]);
		curAcc = p->acc.Length();
		if(curAcc > maxAcc) maxAcc=curAcc;
	}

	if(maxAcc > 0.0f)
		timeStep = kernel_radius/maxAcc*0.4f;
	else
		timeStep = 0.002f;
}

void SPHSystem::buildGrid(){
	for(uint i=0; i<totCell; i++) cells[i].head = NULL;
	Particle *p;
	uint hash;
	for(uint i=0; i<numParticle; i++){
		p = &(particles[i]);
		hash = calcCellHash(calcCellPos(p->pos));

		if(cells[hash].head == NULL){
			p->next = NULL;
			cells[hash].head = p;
		}
		else{
			p->next = cells[hash].head;
			cells[hash].head = p;
		}
	}
}

void SPHSystem::compDensPressure(){
	Particle *p;
	Particle *np;
	Vec2i cellPos;
	Vec2i nearPos;
	uint hash;
	for(uint k=0; k<numParticle; k++){
		p = &(particles[k]);
		p->dens = 0.0f;
		p->pres = 0.0f;
		cellPos = calcCellPos(p->pos);
		for(int i=-1; i<=1; i++){
			for(int j=-1; j<=1; j++){
				nearPos.x = cellPos.x + i;
				nearPos.y = cellPos.y + j;
				hash = calcCellHash(nearPos);

				if(hash == 0xffffffff) continue;

				np = cells[hash].head;

				while(np != NULL){
					Vec2f distVec = np->pos - p->pos;
					float dist2 = distVec.LengthSquared();
					
					if(dist2<INF || dist2>=kernel_radius*kernel_radius){
						np = np->next;
						continue;
					}

					p->dens = p->dens + mass * poly6(dist2);
					np = np->next;
				}
			}
		}
		p->dens = p->dens + mass*poly6(0.0f);
		p->pres = (pow(p->dens / restDensity, 7) - 1) * stiffness;
	}
}

void SPHSystem::compForce(){
	Particle *p;
	Particle *np;
	Vec2i cellPos;
	Vec2i nearPos;
	uint hash;
	for(uint k=0; k<numParticle; k++){
		p = &(particles[k]);
		p->acc = Vec2f(0.0f, 0.0f);
		cellPos = calcCellPos(p->pos);
		for(int i=-1; i<=1; i++){
			for(int j=-1; j<=1; j++){
				nearPos.x = cellPos.x + i;
				nearPos.y = cellPos.y + j;
				hash = calcCellHash(nearPos);

				if(hash == 0xffffffff) continue;

				np = cells[hash].head;
				while(np != NULL){
					Vec2f distVec = p->pos - np->pos;
					float dist2 = distVec.LengthSquared();

					if(dist2 < kernel_radius*kernel_radius && dist2 > INF){
						float dist = sqrt(dist2);
						float V = mass / p->dens;

						float tempForce = V * (p->pres+np->pres) * spiky(dist);
						p->acc = p->acc - distVec*tempForce/dist;

						Vec2f relVel;
						relVel = np->ev-p->ev;
						tempForce = V * viscosity * visco(dist);
						p->acc = p->acc + relVel*tempForce; 
					}

					np = np->next;
				}
			}
		}
		p->acc = p->acc/p->dens+gravity;
	}
}

void SPHSystem::advection(){
	Particle *p;
	for(uint i=0; i<numParticle; i++){
		p = &(particles[i]);
		p->vel = p->vel+p->acc*timeStep;
		p->pos = p->pos+p->vel*timeStep;

		if(p->pos.x < 0.0f){
			p->vel.x = p->vel.x * wallDamping;
			p->pos.x = 0.0f;
		}
		if(p->pos.x >= worldSize.x){
			p->vel.x = p->vel.x * wallDamping;
			p->pos.x = worldSize.x - 0.0001f;
		}
		if(p->pos.y < 0.0f){
			p->vel.y = p->vel.y * wallDamping;
			p->pos.y = 0.0f;
		}
		if(p->pos.y >= worldSize.y){
			p->vel.y = p->vel.y * wallDamping;
			p->pos.y = worldSize.y - 0.0001f;
		}

		p->ev=(p->ev+p->vel)/2;
	}
}

void SPHSystem::animation()
{
	buildGrid();
	compDensPressure();
	compForce();
	//compTimeStep();
	advection();
}
