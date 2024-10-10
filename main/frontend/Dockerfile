FROM node:18-alpine AS build

WORKDIR /app

COPY package*.json ./
RUN npm install

COPY . .

RUN npm run build

FROM alpine:latest AS export-stage

COPY --from=build /app/dist /dist
