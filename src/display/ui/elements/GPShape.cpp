#include "GPShape.h"

void GPShape::draw() {
    uint16_t baseX = this->x;
    uint16_t baseY = this->y;

    // scale to viewport
    GPViewportTransform transform = this->getViewportTransform();
    double scaleX = transform.scaleX;
    double scaleY = transform.scaleY;
    uint16_t offsetX = transform.offsetX;
    uint16_t offsetY = transform.offsetY;

    if (scaleX > 0.0f) {
        baseX = ((this->x) * scaleX + this->getViewport().left) + offsetX;
    }

    if (scaleY > 0.0f) {
        baseY = (this->y) * scaleY + this->getViewport().top + offsetY;
    }

    // base
    if (this->_shape == GP_SHAPE_ELLIPSE) {
        uint16_t scaledSize = (uint16_t)((double)this->_sizeX * scaleX);
        uint16_t baseRadius = (uint16_t)scaledSize;

        getRenderer()->drawEllipse(baseX, baseY, baseRadius, baseRadius, this->strokeColor, this->fillColor);
    } else if (this->_shape == GP_SHAPE_SQUARE) {
        uint16_t sizeX = (this->_sizeX) * scaleX + this->getViewport().left + offsetX;
        uint16_t sizeY = (this->_sizeY) * scaleY + this->getViewport().top + offsetY;

        getRenderer()->drawRectangle(baseX, baseY, sizeX, sizeY, this->strokeColor, this->fillColor, this->_angle);
    } else if (this->_shape == GP_SHAPE_LINE) {
        getRenderer()->drawLine(baseX, baseY, (this->_sizeX) * scaleX + this->getViewport().left + offsetX, (this->_sizeY) * scaleY + this->getViewport().top + offsetY, this->strokeColor, 0);
    } else if (this->_shape == GP_SHAPE_POLYGON) {
        uint16_t scaledSize = (uint16_t)((double)this->_sizeX * scaleX);
        uint16_t baseRadius = (uint16_t)scaledSize;

        getRenderer()->drawPolygon(baseX, baseY, baseRadius, this->_sizeY, this->strokeColor, this->fillColor, this->_angle);
    } else if (this->_shape == GP_SHAPE_ARC) {
        uint16_t scaledSize = (uint16_t)((double)this->_sizeX * scaleX);
        uint16_t baseRadius = (uint16_t)scaledSize;

        getRenderer()->drawArc(baseX, baseY, baseRadius, baseRadius, this->strokeColor, this->fillColor, this->_angle, this->_angleEnd, this->_closed);
    } else if (this->_shape == GP_SHAPE_PILL) {
        uint16_t sizeX = (this->_sizeX) * scaleX + this->getViewport().left + offsetX;
        uint16_t sizeY = (this->_sizeY) * scaleY + this->getViewport().top + offsetY;

        getRenderer()->drawPill(baseX, baseY, sizeX, sizeY, this->strokeColor, this->fillColor, this->_angle);
    }
}
